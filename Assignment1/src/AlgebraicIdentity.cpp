#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

// definizione del pass
struct AlgebraicIdentityPass : PassInfoMixin<AlgebraicIdentityPass> {

  // applicazione del pass su ogni funzione 
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {

        bool modified = false;

    // scorro tutti i blocchi, per ogni blocco scorro tutte le istruzioni
    for (auto &BB : F) {
      for (auto &I : BB) {

        // se l'istruzione è un'istruzione matematica
        if (auto *binOp = dyn_cast<BinaryOperator>(&I)) {
        
          // assegno come operando1, il valore del primo operando
          // assegno come operando2, il valore del secondo operando
          Value *op1 = binOp->getOperand(0);
          Value *op2 = binOp->getOperand(1);

          // esistono 4 tipologie possibili di casi:
          // caso1: x + 0 --> x
          // caso2: 0 + x --> x
          // caso3: x * 1 --> x 
          // caso4: 1 * x --> x

          // ======================
          // Caso1 e Caso2: x + 0 → x oppure 0 + x --> x
          // ======================
          //se è un'operazione di add
          if (binOp->getOpcode() == Instruction::Add) {

            // se il secondo operando è costante
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {
              
              //CASO 1
              // se l'operando costante == 0: allora sostituisco tutti gli usi futuri dell'istruzione binOp con operando1
              if (constOp->isZero()) {
                binOp->replaceAllUsesWith(op1);
                binOp->eraseFromParent();
                modified = true; 
                continue;
              }
            }
    
            // se il primo operando è costante
            if (auto *constOp = dyn_cast<ConstantInt>(op1)) {
            
              // se l'operando costante == 0: allora sostituisco gli usi futuri dell'istruzione binOp con operando2
              if (constOp->isZero()) {
                binOp->replaceAllUsesWith(op2);
                binOp->eraseFromParent();
                modificed = true;
                continue;
              }
            }
          }

          // ======================
          // Caso3 e Caso4: x * 1 → x oppure 1 * X --> X
          // ======================
          // se è un'operazione di mul
          if (binOp->getOpcode() == Instruction::Mul) {

            // se il secondo operando è costante e == 1, sostituisce gli usi futuri di binOp con il primo operando
            if (auto *constOp = dyn_cast<ConstantInt>(op2)) {
              if (constOp->isOne()) {
                binOp->replaceAllUsesWith(op1);
                binOp->eraseFromParent();
                modified = true;
                continue;
              }
            }
            
            // se il primo operando è costante e == 1, sostituisce gli usi futuri di binOp con il secondo operando
            if (auto *constOp = dyn_cast<ConstantInt>(op1)) {
              if (constOp->isOne()) {
                binOp->replaceAllUsesWith(op2);
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