#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h" 
#include "llvm/IR/Dominators.h"     
#include <set>
#include <vector>

using namespace llvm;

namespace {


struct LoopPass : public PassInfoMixin<LoopPass> {
    
    // Funzione per verificare se un valore è loop-invariant
    bool isValueInvariant(Value *V, Loop *L, const std::set<Instruction*> &Invariants) {
        // Se è una costante o un argomento della funzione, è invariante
        if (isa<Constant>(V) || isa<Argument>(V)) {
            return true;
        }

        // Se è un'istruzione, verifichiamo dove è definita
        if (Instruction *I = dyn_cast<Instruction>(V)) {
            // Se è definita fuori dal loop, è invariante
            if (!L->contains(I->getParent())) {
                return true;
            }
            // Se è definita dentro, è invariante solo se è già stata marcata come invariante
            if (Invariants.count(I)) {
                return true;
            }
        }

        return false;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F); 
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);


        if (LI.empty()) {
            errs() << "Nessun Loop trovato in questa funzione.\n";
            return PreservedAnalyses::all();
        }

       
        bool Changed = false;

        // Iteriamo su tutti i loop della funzione
        for (Loop *L : LI) {
            errs() << "\n========================================\n";
            errs() << " Analizzo Loop: " << L->getHeader()->getName() << "\n";

            // LICM richiede che il loop sia in forma semplificata (deve avere un preheader valido)
            if (!L->isLoopSimplifyForm()) {
                errs() << " [!] Il loop NON e' in forma canonica semplificata. Salto.\n";
                continue;
            }

            BasicBlock *Preheader = L->getLoopPreheader();
            if (!Preheader) {
                errs() << " [!] Preheader non trovato. Salto.\n";
                continue;
            }
            errs() << " [*] Preheader trovato correttamente.\n";

            
            // IDENTIFICAZIONE DELLE ISTRUZIONI LOOP-INVARIANT
            
            std::set<Instruction*> InvariantInstructions;
            bool NewInvariantFound;

            // Ripetiamo in loop finché non troviamo più nuove istruzioni invarianti (punto fisso)
            do {
                NewInvariantFound = false;

                for (BasicBlock *BB : L->blocks()) {
                    for (Instruction &I : *BB) {
                        //salto istruzioni come salti, o operazioni di memoria, o con side effect (es. i++)
                        if (I.mayHaveSideEffects() || I.mayReadFromMemory() || I.isTerminator()) {
                            continue;
                        }
                        // Se è già marcata, saltiamo
                        if (InvariantInstructions.count(&I)) {
                            continue;
                        }

                        // Verifichiamo se tutti gli operandi sono invarianti
                        bool AllOperandsInvariant = true;
                        for (auto &Op : I.operands()) {
                            if (!isValueInvariant(Op.get(), L, InvariantInstructions)) {
                                AllOperandsInvariant = false;
                                break;
                            }
                        }

                        if (AllOperandsInvariant) {
                            InvariantInstructions.insert(&I);
                            NewInvariantFound = true;
                            errs() << "Trovata istruzione Invariante: " << I << "\n";
                        }
                    }
                }
            } while (NewInvariantFound);

            
            // CONDIZIONI DI DOMINANZA ED EVENTUALE CODE MOTION
           

            SmallVector<BasicBlock*, 4> ExitBlocks;
            L->getExitBlocks(ExitBlocks);

            // Manteniamo l'ordine di dipendenza usando un vettore per lo spostamento
            std::vector<Instruction*> InstructionsToMove;

            for (BasicBlock *BB : L->blocks()) {
                for (Instruction &I : *BB) {
                    if (InvariantInstructions.count(&I)) {
                        
                        // Verifico se il blocco dell'istruzione domina tutte le uscite del loop
                        bool DominatesAllExits = true;
                        for (BasicBlock *ExitBB : ExitBlocks) {
                            if (!DT.dominates(I.getParent(), ExitBB)) {
                                DominatesAllExits = false;
                                break;
                            }
                        }

                        if (!DominatesAllExits) {
                            // verifichiamo se è "dead" fuori dal loop
                            bool UsedOutsideLoop = false;
                            for (User *U : I.users()) {
                                if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                                    //Se c'è anche un solo utilizzo di UserInst in un blocco che non fa parte del loop
                                    if (!L->contains(UserInst->getParent())) {
                                        UsedOutsideLoop = true;
                                        break;
                                    }
                                }
                            }

                            if (UsedOutsideLoop) {
                                // Fallisce: Non domina le uscite ed è usata fuori
                                continue;
                            } else {
                                errs() << "L'istruzione [" << I << "] e' dead fuori dal loop, quindi posso spostarla\n";
                            }
                        }

                        // La definizione domina tutti i blocchi in cui viene usata nel loop
                        bool DominatesAllUses = true;
                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                                // Ci interessano solo gli usi interni al loop
                                if (L->contains(UserInst->getParent())) {
                                    
                                    if (!DT.dominates(I.getParent(), UserInst->getParent())) {
                                        DominatesAllUses = false;
                                        break;
                                    }
                                
                                }
                            }
                        }

                        if (!DominatesAllUses) {
                            // Fallisce: Non domina tutti i suoi usi interni
                            continue;
                        } else {
                            errs() << "L'istruzione [" << I<< "] si puo' spostare (domina tutti i suoi usi): \n";    
                        }

                        // Superate le verifiche, l'istruzione è candidata allo spostamento
                        InstructionsToMove.push_back(&I);
                    }
                }
            }

            // Spostamento effettivo nel preheader delle istruzioni validate
            if (!InstructionsToMove.empty()) {
                Instruction *InsertPos = Preheader->getTerminator(); // Inseriamo subito prima del salto finale del preheader
                
                for (Instruction *I : InstructionsToMove) {
                    errs() << " >>> SPOSTO NEL PREHEADER: " << *I << "\n";
                    I->moveBefore(InsertPos); // Sposta fisicamente l'istruzione
                }
                Changed = true;
            }
        }

        // Se abbiamo modificato il codice, notifichiamo LLVM che i vecchi passaggi di analisi CFG potrebbero non essere puliti
        return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "LoopPass") {
                            FPM.addPass(LoopPass());
                            return true;
                        }
                        return false;
                    });
            }};
}