#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

struct MultiInstructionPass : PassInfoMixin<MultiInstructionPass> {

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    bool modified = false;

    for (auto &BB : F) {
      for (auto It = BB.begin(); It != BB.end();) {
        
        // 'c' è l'istruzione che stiamo guardando in questo momento
        Instruction &c_inst = *It++;

        auto *c = dyn_cast<BinaryOperator>(&c_inst);
        if (!c) continue;

        Instruction::BinaryOps Opcode = c->getOpcode();

        // =====================================================================
        // CASO 1: Esempio del testo -> [ a = b + 1,  c = a - 1 ]  =>  c = b
        // =====================================================================
        if (Opcode == Instruction::Sub) {
          
          // In (a - 1), l'operando destro (1) DEVE essere una costante
          auto *costante_sub = dyn_cast<ConstantInt>(c->getOperand(1));
          
          // L'operando sinistro (0) DEVE essere l'istruzione 'a'
          auto *a = dyn_cast<BinaryOperator>(c->getOperand(0));

          // Verifichiamo che 'a' sia davvero un'Addizione
          if (costante_sub && a && a->getOpcode() == Instruction::Add) {
            
            Value *op0 = a->getOperand(0);
            Value *op1 = a->getOperand(1);

            // Commutatività di 'a': l'addizione può essere (b + 1) oppure (1 + b)
            ConstantInt *costante_add = dyn_cast<ConstantInt>(op1);
            Value *b = op0;

            if (!costante_add) { // Se a destra non c'era il +1, proviamo a sinistra
              costante_add = dyn_cast<ConstantInt>(op0);
              b = op1;
            }

            // Se il "+1" dell'addizione e il "-1" della sottrazione sono lo stesso numero:
            if (costante_add && costante_add->getValue() == costante_sub->getValue()) {
              c->replaceAllUsesWith(b);
              //a questo punto dopo replacealluses c non è più utilizzata perchè rimpiazzata. 
              c->eraseFromParent();
              modified = true;
              errs()<<"Trovata caso seconda istruzione sub ";
              continue;
            }
          }
        }

        // =====================================================================
        // CASO 2: Il simmetrico   -> [ a = b - 1,  c = a + 1 ]  =>  c = b
        // =====================================================================
        else if (Opcode == Instruction::Add) {
          
          Value *op0 = c->getOperand(0);
          Value *op1 = c->getOperand(1);

          // Commutatività di 'c': l'addizione esterna può essere (a + 1) oppure (1 + a)
          ConstantInt *costante_add = dyn_cast<ConstantInt>(op1);
          BinaryOperator *a = dyn_cast<BinaryOperator>(op0);
            //a qui è intesa come variabile o costante
          if (!costante_add || !a) {
            costante_add = dyn_cast<ConstantInt>(op0);
            a = dyn_cast<BinaryOperator>(op1);
          }

          // Verifichiamo che 'a' (operazione) sia una Sottrazione
          if (costante_add && a && a->getOpcode() == Instruction::Sub) {
            
            // Nella sottrazione (b - 1) la costante è fissa a destra, 'b' a sinistra
            Value *b = a->getOperand(0);
            auto *costante_sub = dyn_cast<ConstantInt>(a->getOperand(1));

            if (costante_sub && costante_sub->getValue() == costante_add->getValue()) {
              c->replaceAllUsesWith(b);
              c->eraseFromParent();
              modified = true;
               errs()<<"Trovata caso seconda istruzione add ";
              continue;
            }
          }
        }

        // =====================================================================
        // CASO 3: Moltiplicazione -> [ a = b * 2,  c = a / 2 ]  =>  c = b
        // =====================================================================
        else if (Opcode == Instruction::SDiv || Opcode == Instruction::UDiv) {
          
          // Nella divisione (a / 2) la costante sta per forza a destra
          auto *costante_div = dyn_cast<ConstantInt>(c->getOperand(1));
          auto *a = dyn_cast<BinaryOperator>(c->getOperand(0));

          // Sicurezza: costante_div non deve essere 0
          if (costante_div && !costante_div->isZero() && a && a->getOpcode() == Instruction::Mul) {
            //intesi come gli operazndi della moltiplicazione a
            Value *op0 = a->getOperand(0);
            Value *op1 = a->getOperand(1);

            // Commutatività di 'a': (b * 2) oppure (2 * b)
            ConstantInt *costante_mul = dyn_cast<ConstantInt>(op1);
            Value *b = op0;

            if (!costante_mul) {
              costante_mul = dyn_cast<ConstantInt>(op0);
              b = op1;
            }

            if (costante_mul && costante_mul->getValue() == costante_div->getValue()) {
              c->replaceAllUsesWith(b);
              c->eraseFromParent();
              modified = true;
              continue;
            }
          }
        }

      }
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};