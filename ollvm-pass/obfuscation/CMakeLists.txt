file(GLOB obfuscation_sources
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(LLVMObfuscationx SHARED ${obfuscation_sources})

target_include_directories(LLVMObfuscationx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(LLVMObfuscationx PRIVATE ${LLVM_LIBS})
