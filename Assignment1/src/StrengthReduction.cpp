#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath> //per considerare le potenze di 2

using namespace llvm;

static bool isPowerOfTwo(int val) {
    
    // non è valido
    if (val <= 0) 
        return false;

    // definisco il numero potenza di 2
    return (val & (val - 1)) == 0;
}

// definizione del passo 
struct StrengthReductionPass : PassInfoMixin<StrengthReductionPass> {

  // prendo ogni funzione
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {

    // mi dice se ho già modificato qualcosa
    bool modified = false;

    // scorro ogni bb di ogni funzione, e ogni istruzione di ogni bb 
    for (auto &BB : F) {
      for (auto it = BB.begin(); it != BB.end();) {

        Instruction *I = &*it;
        ++it;

        // controllo che l'istruzione sia matematica e inserisco i valori in operatore1 e operatore2
        if (auto *binOp = dyn_cast<BinaryOperator>(I)) {

          Value *op1 = binOp->getOperand(0);
          Value *op2 = binOp->getOperand(1);

          // CASO BASE 1 --> x * 2^n 
          // controllo se l'operazione è una moltiplicazione
          if (binOp->getOpcode() == Instruction::Mul) { 

            // controllo se il secondo operando è costante
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {
            
              // estraggo il valore numerico dall'operatore  
              int val = constOp->getSExtValue();

              // controllo se è potenza di 2
              if (isPowerOfTwo(val)) {

                // calcolo di quanto devo shiftare
                int shift = llvm::Log2_32(val);

                // creo una nuova istruzione di shift (op1 << val)
                IRBuilder<> builder(binOp);
                Value *newInst = builder.CreateShl(op1, ConstantInt::get(op1->getType(), shift));
                
                // sostituisco tutti gli usi dell'istruzione di mul con quella di shift 
                binOp->replaceAllUsesWith(newInst);
                binOp->eraseFromParent();

                modified = true; // è stato modificato qualcosa
                continue; // continuo con gli altri casi
              }
            }
          }

          // CASO BASE 2 --> X / 2^n
          // controllo se l'operazione è una divisione (signed o unsigned)
          if (binOp->getOpcode() == Instruction::SDiv || binOp->getOpcode() == Instruction::UDiv) {

            // controllo se il secondo operando è costante
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {

              // estraggo il valore dell'operando
              int val = constOp->getSExtValue();

              if (isPowerOfTwo(val)) {

                // creo il valore dello shift
                int shift = llvm::Log2_32(val);

                // genero l'operazione di shift 
                IRBuilder<> builder(binOp);

                Value *newInst;

                // ho bisogno di 2 tipi: il primo mantiene il segno e il secondo no 
                if (binOp->getOpcode() == Instruction::SDiv)
                  newInst = builder.CreateAShr(op1, ConstantInt::get(op1->getType(), shift));
                else
                  newInst = builder.CreateLShr(op1, ConstantInt::get(op1->getType(), shift));

                // sostituisco gli usi dell'istruzione div con quella di shift  
                binOp->replaceAllUsesWith(newInst);
                binOp->eraseFromParent();

                modified = true;
                continue;
              }
            }
          }

          // CASO AVANZATO 3 --> x * (2^n -1) diventa (x << n) -x 
          if (binOp->getOpcode() == Instruction::Mul) {

            // controllo che l'oprando sia costante, e nel caso prendo il suo valore
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {

                int val = constOp->getSExtValue();

                // controllo se il valore è quello richiesto
                if (val > 0 && isPowerOfTwo(val + 1)) {

                    // genero lo shift
                    int shift = llvm::Log2_32(val + 1);

                    // genero l'istruzione di shift e di sub
                    IRBuilder<> builder(binOp);
                    Value *shifted = builder.CreateShl(op1, ConstantInt::get(op1->getType(), shift));
                    Value *newInst = builder.CreateSub(shifted, op1);
                    
                    // sostituisco gli usi
                    binOp->replaceAllUsesWith(newInst);
                    binOp->eraseFromParent();

                    modified = true;
                    continue;
                }
            }
          } 
        }
      }
    }

    // se modified == True allora restituisco none, altrimenti all 
    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};