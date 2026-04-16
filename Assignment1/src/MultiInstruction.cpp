#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

struct MultiInstructionPass : PassInfoMixin<MultiInstructionPass> {

  // per ogni funzione
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {

    // flag di modifica
    bool modified = false;

    // scorro ogni bb di ogni funzione, e ogni istruzione di ogni bb 
    for (auto &BB : F) {
      for (auto &I : BB) {

        // controllo se è un operazione matematica (quindi la seconda istruzione del nostro pattern)
        if (auto *sub = dyn_cast<BinaryOperator>(&I)) {

          // controllo se è una sottrazione
          if (sub->getOpcode() == Instruction::Sub) {

            // inserisco i valori all'interno di operando1 e operando2 di istruzione 2
            Value *op1 = sub->getOperand(0);
            Value *op2 = sub->getOperand(1);

            // op1 deve essere il risultato di una add (quindi la prima istruzione del mio pattern)
            if (auto *add = dyn_cast<BinaryOperator>(op1)) {

              // controllo che operando1 sia il risultato di una add
              if (add->getOpcode() == Instruction::Add) {

                // inserisco il primo operando della prima istruzione, in b
                // inserisco il secondo operando della prima istruzione, in n1
                // inserisco il secondo operando della seconda istruzione, in n2
                Value *b = add->getOperand(0);
                Value *n1 = add->getOperand(1);
                Value *n2 = op2;

                // controlla che n sia costante (non deve cambiare ovviamente)
                if (auto *c1 = dyn_cast<ConstantInt>(n1)) {
                  if (auto *c2 = dyn_cast<ConstantInt>(n2)) {

                    // controlla n1 ed n2 siano uguali
                    if (c1->getSExtValue() == c2->getSExtValue()) {

                      // sostituisco tutti gli usi
                      // sostituisci tutti i c direttamente con b
                      sub->replaceAllUsesWith(b);
                      sub->eraseFromParent();

                      modified = true;
                      continue;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    // restituisce in base a se è stato modificato o meno 
    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};