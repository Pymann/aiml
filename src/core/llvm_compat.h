// llvm_compat.h — the few LLVM interfaces that differ between LLVM 18, which aiml
// builds with, and the newer LLVM the llvm-update workflow moves it to
// (TEST_SUITES.md §4.2).  Everything else is written in the spelling both accept
// (IRBuilder::CreateGlobalString, StringRef::starts_with, a DataLayout copied
// from Module::getDataLayout()), so only these two need a version test.
#pragma once

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#include <string>

namespace aiml_llvm {

// A target triple as Module::setTargetTriple, TargetRegistry::lookupTarget and
// Target::createTargetMachine take it: llvm::Triple from LLVM 21 on, a string
// before.
#if LLVM_VERSION_MAJOR >= 21
inline llvm::Triple triple(const std::string& t) { return llvm::Triple(t); }
#else
inline std::string triple(const std::string& t) { return t; }
#endif

// The declaration of an intrinsic without overloaded types, made when missing:
// Intrinsic::getDeclaration until LLVM 19, getOrInsertDeclaration from LLVM 20.
inline llvm::Function* intrinsic(llvm::Module* m, llvm::Intrinsic::ID id) {
#if LLVM_VERSION_MAJOR >= 20
    return llvm::Intrinsic::getOrInsertDeclaration(m, id);
#else
    return llvm::Intrinsic::getDeclaration(m, id);
#endif
}

}  // namespace aiml_llvm
