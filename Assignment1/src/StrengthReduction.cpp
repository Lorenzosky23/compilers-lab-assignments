#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath> // per considerare le potenze di 2

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

        // controllo che l'istruzione sia matematica
        if (auto *binOp = dyn_cast<BinaryOperator>(I)) {

          Value *op1 = binOp->getOperand(0);
          Value *op2 = binOp->getOperand(1);

          // ===================================================================
          // CASO 1 e 3: MOLTIPLICAZIONE ( x * 2^n ) oppure ( x * (2^n - 1) )
          // ===================================================================
          if (binOp->getOpcode() == Instruction::Mul) { 

            ConstantInt *constOp = nullptr;
            Value *varOp = nullptr;

            // GESTIONE COMMUTATIVITÀ PULITA E LINEARE:
            // Guardiamo a destra. Se c'è un numero fisso, la variabile è a sinistra.
            if (auto *c = dyn_cast<ConstantInt>(op2)) {
              constOp = c;
              varOp = op1;
            } 
            // Altrimenti guardiamo a sinistra. Se il numero è lì, la variabile è a destra.
            else if (auto *c = dyn_cast<ConstantInt>(op1)) {
              constOp = c;
              varOp = op2;
            }

            // Se abbiamo trovato sia la costante che la variabile:
            if (constOp && varOp) {
              int val = constOp->getSExtValue();

              // Sotto-caso A (Base) --> x * 2^n diventa (x << n)
              if (isPowerOfTwo(val)) {
                int shift = llvm::Log2_32(val);
                IRBuilder<> builder(binOp);
                Value *newInst = builder.CreateShl(varOp, ConstantInt::get(varOp->getType(), shift));
                
                binOp->replaceAllUsesWith(newInst);
                binOp->eraseFromParent();

                modified = true;
                continue; 
              }
              // Sotto-caso B (Avanzato) --> x * (2^n - 1) diventa (x << n) - x 
              else if (val > 0 && isPowerOfTwo(val + 1)) {
                int shift = llvm::Log2_32(val + 1);
                IRBuilder<> builder(binOp);
                
                Value *shifted = builder.CreateShl(varOp, ConstantInt::get(varOp->getType(), shift));
                Value *newInst = builder.CreateSub(shifted, varOp);
                
                binOp->replaceAllUsesWith(newInst);
                binOp->eraseFromParent();

                modified = true;
                continue;
              }
            }
          }

          // ===================================================================
          // CASO 2: DIVISIONE --> X / 2^n (Qui l'ordine è fisso, la costante sta a destra)
          // ===================================================================
          else if (binOp->getOpcode() == Instruction::SDiv || binOp->getOpcode() == Instruction::UDiv) {

            // controllo se il secondo operando è costante
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {

              int val = constOp->getSExtValue();

              if (isPowerOfTwo(val)) {
                int shift = llvm::Log2_32(val);
                IRBuilder<> builder(binOp);
                Value *newInst;

                // Divisione con segno (AShr mantiene il bit di segno) o senza segno (LShr)
                if (binOp->getOpcode() == Instruction::SDiv)
                  newInst = builder.CreateAShr(op1, ConstantInt::get(op1->getType(), shift));
                else
                  newInst = builder.CreateLShr(op1, ConstantInt::get(op1->getType(), shift));

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

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};