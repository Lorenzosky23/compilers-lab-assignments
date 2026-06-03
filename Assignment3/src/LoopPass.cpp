#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

class LoopPass : public PassInfoMixin<LoopPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    // 1. Otteniamo l'analisi LoopInfo
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

    if (LI.empty()) {
      return PreservedAnalyses::all();
    }

    // 2. Scorrimento BB per identificare header
    for (BasicBlock &BB : F) {
      if (LI.isLoopHeader(&BB)) {
        errs() << "BB Header trovato: ";
        BB.printAsOperand(errs(), false);
        errs() << "\n\n";
      }
    }

    // 3. Scorrimento dei loop
    for (Loop *L : LI) {
      // a) Verifica forma normale (LoopSimplify)
      errs() << "Loop in forma normale: " << (L->isLoopSimplifyForm() ? "Si" : "No") << "\n\n";

      // b) Recupero header e funzione tramite header
      BasicBlock *Header = L->getHeader();
      Function *ParentFunc = Header->getParent(); // Recupero tramite BB
      errs() << "CFG della funzione " << ParentFunc->getName() << ":\n\n";
      ParentFunc->print(errs(), nullptr);

      // c) Stampa blocchi del loop
      errs() << "Blocchi nel loop:\n";
      for (BasicBlock *BB : L->getBlocks()) {
        errs() << " - ";
        BB->printAsOperand(errs(), false);
        errs() << "\n\n";
      }
    }

    return PreservedAnalyses::all();
  }
};
// -----------------------------------------------------------------------------------
// Punto di ingresso per il Nuovo Pass Manager
// -----------------------------------------------------------------------------------
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "LoopPass", LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "loop-pass") {
            FPM.addPass(LoopPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}