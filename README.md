# ollvm-pass

基于 [0xlane/ollvm-rust](https://github.com/0xlane/ollvm-rust) 修改，适配 LLVM 21 的 LLVM 混淆插件。通过 `rustc` 或 `opt` 动态加载，无需重新编译 LLVM/Rust。

## 功能

支持以下混淆方式：
- **间接跳转** (`-irobf-indbr`)：加密跳转目标地址。
- **间接函数调用** (`-irobf-icall`)：加密目标函数地址。
- **间接全局变量引用** (`-irobf-indgv`)：加密全局变量地址。
- **字符串加密** (`-irobf-cse`)：加密 C 风格字符串（Rust 中不生效，已知问题）。
- **控制流平坦化** (`-irobf-cff`)：平坦化控制流。
- **全部混淆**：组合以上方式。

**注意**：仅在 GNU/Linux 测试，其他平台未验证。

## 安装

1. 安装 LLVM 21 和 `clang-21`、`clang++-21`。
2. 编译插件：
   ```bash
   cmake -G "Ninja" -S . -B build \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_C_COMPILER=clang-21 \
         -DCMAKE_CXX_COMPILER=clang++-21 \
         -DLLVM_DIR=/usr/lib/llvm-21/cmake
   cmake --build build -j $(nproc)
   ```

## 使用

### Rust 动态加载
需要 Rust nightly 工具链：
```bash
rustup toolchain install nightly
cargo new helloworld --bin
cd helloworld
 cargo +nightly rustc --release -- -Zllvm-plugins="/path/to/libLLVMObfuscationx.so" -Cpasses="irobf(irobf-indbr,irobf-icall,irobf-indgv,irobf-cse)"
```

### Opt 动态加载
```bash
clang -emit-llvm -c input.c -o input.bc
opt -load-pass-plugin="/path/to/LLVMObfuscationx.so" --passes="irobf(irobf-indbr,irobf-icall,irobf-indgv,irobf-cse)" input.bc -o output.bc
llc -filetype=obj output.bc -o output.o
clang output.o -o output
```

## 已知问题
- 字符串加密 (`-irobf-cse`) 在 Rust 中不生效（[参考](https://github.com/joaovarelas/Obfuscator-LLVM-16.0/issues/8)）。
- 控制流平坦化 (`-irobf-cff`) 未修复。
- 仅 GNU/Linux 测试，Windows/macOS 未验证。

## 参考
- [Allow loading of LLVM plugins [when dynamically built rust]](https://github.com/rust-lang/rust/pull/82734)
- [Installing from Source](https://github.com/rust-lang/rust/blob/master/INSTALL.md)
- [rustc dev guide](https://rustc-dev-guide.rust-lang.org/building/how-to-build-and-run.html)
- [Windows rust使用LLVM pass](https://bbs.kanxue.com/thread-274453.htm)
- [Orust Mimikatz Bypass Kaspersky](https://b1n.io/posts/orust-mimikatz-bypass-kaspersky/)
- [Building LLVM with CMake](https://llvm.org/docs/CMake.html#developing-llvm-passes-out-of-source)
- [llvm-tutor](https://github.com/banach-space/llvm-tutor)
- [String encryption failed](https://github.com/joaovarelas/Obfuscator-LLVM-16.0/issues/8)
## 致谢
- [Arkari](https://github.com/KomiMoe/Arkari)：原始项目。
