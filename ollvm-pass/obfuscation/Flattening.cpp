//===- Flattening.cpp - Control Flow Flattening for LLVM 21 ---------------===//
//
// Adapted from HikariObfuscator for modern LLVM (v21).
// This is a proper, self-contained New Pass Manager (NPM) implementation.
//
//===----------------------------------------------------------------------===//

// ==================== LLVM Headers ====================
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/Local.h"

// ==================== Local Project Headers ====================
#include "include/CryptoUtils.h"
#include "include/Utils.h"
// ================================================================

#include <unordered_map>

using namespace llvm;

#define DEBUG_TYPE "cffobf"

namespace {

void fixStack(Function &F) {
  if (F.empty()) return;
  std::vector<AllocaInst *> AllocaToMove;
  BasicBlock &EntryBB = F.getEntryBlock();
  auto FirstInsertionPt = EntryBB.getFirstInsertionPt();

  for (BasicBlock &BB : F) {
    if (&BB == &EntryBB) continue;
    for (Instruction &I : BB) {
      if (auto *AI = dyn_cast<AllocaInst>(&I)) {
        AllocaToMove.push_back(AI);
      }
    }
  }

  for (AllocaInst *AI : AllocaToMove) {
    AI->moveBefore(FirstInsertionPt);
  }
}


struct Flattening : public PassInfoMixin<Flattening> {
  bool flag = true;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  void flatten(Function &F);
};

PreservedAnalyses Flattening::run(Function &F, FunctionAnalysisManager &FAM) {
  if (toObfuscate(flag, &F, "fla") && !F.isPresplitCoroutine()) {
    errs() << "Running ControlFlowFlattening On " << F.getName() << "\n";
    flatten(F);
    return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}

void Flattening::flatten(Function &F) {
  if (F.empty()) return;
  
  // ==================== 最终修复：添加启发式过滤器 ====================
  // 如果函数太复杂（基本块超过30个），就跳过它。
  // 这是一个简单但有效的启发式方法，可以避免处理我们无法正确处理的复杂编译器内部函数。
  if (F.size() > 30) {
      errs() << "Skipping " << F.getName() << " (too complex: >30 basic blocks)\n";
      return;
  }
  // =================================================================

  std::vector<PHINode*> phis;
  for (BasicBlock &BB : F) {
    for (PHINode &PN : BB.phis()) {
      phis.push_back(&PN);
    }
  }
  for (PHINode *PN : phis) {
    DemotePHIToStack(PN);
  }
  
  SmallVector<BasicBlock *, 8> origBB;
  for (BasicBlock &BB : F) {
    if (BB.isEHPad() || BB.isLandingPad()) {
      errs() << F.getName() << " contains exception handling, unsupported.\n";
      return;
    }
    origBB.push_back(&BB);
  }

  if (origBB.size() <= 1) return;

  char scrambling_key[16];
  cryptoutils->get_bytes(scrambling_key, 16);

  BasicBlock *entryBlock = &F.getEntryBlock();
  origBB.erase(origBB.begin()); 

  if (entryBlock->getTerminator()->getNumSuccessors() == 0) return;
  
  if (entryBlock->getTerminator()->getNumSuccessors() > 1) {
      auto split_point = entryBlock->getFirstInsertionPt();
      if(split_point != F.getEntryBlock().end()) {
        entryBlock = entryBlock->splitBasicBlock(split_point, "entry.split");
        origBB.insert(origBB.begin(), entryBlock);
      }
  }
  
  Instruction *oldTerm = entryBlock->getTerminator();
  if (oldTerm->getNumSuccessors() == 0) return;
  BasicBlock *insertPoint = oldTerm->getSuccessor(0);
  
  IRBuilder<> builder(F.getContext());
  BasicBlock *loopEntry = BasicBlock::Create(F.getContext(), "loopEntry", &F, insertPoint);
  BasicBlock *loopEnd = BasicBlock::Create(F.getContext(), "loopEnd", &F, insertPoint);
  BasicBlock *swDefault = BasicBlock::Create(F.getContext(), "switchDefault", &F, loopEnd);
  
  builder.SetInsertPoint(&F.getEntryBlock(), F.getEntryBlock().getFirstInsertionPt());
  AllocaInst *switchVar = builder.CreateAlloca(builder.getInt32Ty(), nullptr, "switchVar");
  
  builder.SetInsertPoint(oldTerm);
  builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), cryptoutils->scramble32(0, scrambling_key)), switchVar);
  oldTerm->eraseFromParent();

  builder.SetInsertPoint(entryBlock);
  builder.CreateBr(loopEntry);

  builder.SetInsertPoint(loopEntry);
  Value *load = builder.CreateLoad(builder.getInt32Ty(), switchVar, "loadState");
  SwitchInst *switchI = builder.CreateSwitch(load, swDefault, origBB.size());
  
  builder.SetInsertPoint(loopEnd);
  builder.CreateBr(loopEntry);
  builder.SetInsertPoint(swDefault);
  builder.CreateBr(loopEnd);

  for (BasicBlock *bb : origBB) {
    bb->moveBefore(loopEnd);
    uint32_t caseNum = cryptoutils->scramble32(switchI->getNumCases(), scrambling_key);
    switchI->addCase(builder.getInt32(caseNum), bb);
  }

  for (BasicBlock *bb : origBB) {
    if (isa<ReturnInst>(bb->getTerminator())) continue;
    
    Instruction* term = bb->getTerminator();
    builder.SetInsertPoint(bb);
    
    if (auto *br = dyn_cast<BranchInst>(term)) {
      if (br->isConditional()) {
        ConstantInt *caseTrue = switchI->findCaseDest(br->getSuccessor(0));
        ConstantInt *caseFalse = switchI->findCaseDest(br->getSuccessor(1));
        if (!caseTrue || !caseFalse) continue;
        Value *sel = builder.CreateSelect(br->getCondition(), caseTrue, caseFalse);
        builder.CreateStore(sel, switchVar);
      } else {
        ConstantInt *caseNext = switchI->findCaseDest(br->getSuccessor(0));
        if (!caseNext) continue;
        builder.CreateStore(caseNext, switchVar);
      }
    } else if (auto *sw = dyn_cast<SwitchInst>(term)) {
        Value *cond = sw->getCondition();
        ConstantInt *defaultCase = switchI->findCaseDest(sw->getDefaultDest());
        if (!defaultCase) continue;

        Value *nextState = defaultCase;
        for (auto &Case : sw->cases()) {
            ConstantInt *caseValue = Case.getCaseValue();
            ConstantInt *caseDest = switchI->findCaseDest(Case.getCaseSuccessor());
            if (!caseDest) continue;
            Value *cmp = builder.CreateICmpEQ(cond, caseValue);
            nextState = builder.CreateSelect(cmp, caseDest, nextState);
        }
        builder.CreateStore(nextState, switchVar);
    }
    
    term->eraseFromParent();
    builder.CreateBr(loopEnd);
  }
  
  fixStack(F);
  
  // We add a final verification check.
  if (verifyFunction(F, &errs())) {
    errs() << "Error: Flattening pass generated invalid IR for function " << F.getName() << "!\n";
  }
}

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "CFF", "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "cffobf") {
                    FPM.addPass(Flattening());
                    return true;
                  }
                  return false;
                });
          }};
}