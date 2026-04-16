#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

// dichiarazione dei pass
#include "AlgebraicIdentity.cpp"
#include "StrengthReduction.cpp"
#include "MultiInstruction.cpp"

// se viene chiamato un pass specifico, allora viene eseguito quel pass
bool add_passes(StringRef Name, FunctionPassManager &FPM){
  if (Name == "ai") {
    FPM.addPass(AlgebraicIdentityPass());
    return true;
  }
  if (Name == "sr") {
    FPM.addPass(StrengthReductionPass());
    return true;
  }
  if (Name == "mi") {
    FPM.addPass(MultiInstructionPass());
    return true;
  }
  return false;
}

llvm::PassPluginLibraryInfo getPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "Assignment1", LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          return add_passes(Name, FPM);
        }
      );
    }
  };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getPluginInfo();
}