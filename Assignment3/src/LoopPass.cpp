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

// Rinominato per evitare conflitti con il passo ufficiale LLVM
struct CustomLICMPass : public PassInfoMixin<CustomLICMPass> {
    
    // Funzione helper per verificare se un valore è loop-invariant
    bool isValueInvariant(Value *V, Loop *L, const std::set<Instruction*> &Invariants) {
        // Se è una costante o un argomento della funzione, è fuori dal loop ed è invariante
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

        errs() << "\nAnalisi della funzione: " << F.getName() << "\n";

        if (LI.empty()) {
            errs() << "Nessun Loop trovato in questa funzione.\n";
            return PreservedAnalyses::all();
        }

        errs() << "-> Trovati dei loop! Inizio ottimizzazione Custom LICM...\n";
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

            // -----------------------------------------------------------------
            // FASE 1: Identificazione delle istruzioni Loop-Invariant
            // -----------------------------------------------------------------
            std::set<Instruction*> InvariantInstructions;
            bool NewInvariantFound;

            // Ripetiamo in loop finché non troviamo più nuove istruzioni invarianti (punto fisso)
            do {
                NewInvariantFound = false;

                for (BasicBlock *BB : L->blocks()) {
                    for (Instruction &I : *BB) {
                       
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
                            errs() << " -> Trovata istruzione Invariante: " << I << "\n";
                        }
                    }
                }
            } while (NewInvariantFound);

            // -----------------------------------------------------------------
            // FASE 2 & 3: Controllo delle Condizioni di Dominanza e Code Motion
            // -----------------------------------------------------------------
            // Raccogliamo le uscite del loop
            SmallVector<BasicBlock*, 4> ExitBlocks;
            L->getExitBlocks(ExitBlocks);

            // Manteniamo l'ordine di dipendenza usando un vettore per lo spostamento
            std::vector<Instruction*> InstructionsToMove;

            for (BasicBlock *BB : L->blocks()) {
                for (Instruction &I : *BB) {
                    if (InvariantInstructions.count(&I)) {
                        
                        // Condizione A: Il blocco dell'istruzione domina tutte le uscite del loop
                        bool DominatesAllExits = true;
                        for (BasicBlock *ExitBB : ExitBlocks) {
                            if (!DT.dominates(I.getParent(), ExitBB)) {
                                DominatesAllExits = false;
                                break;
                            }
                        }

                    if (!DominatesAllExits) {
                        // Se non domina tutte le uscite, verifichiamo se almeno è "dead" fuori dal loop
                        bool UsedOutsideLoop = false;
                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                                if (!L->contains(UserInst->getParent())) {
                                    UsedOutsideLoop = true;
                                    break;
                                }
                            }
                        }

                        if (UsedOutsideLoop) {
                            errs() << " [!] Non si puo' spostare (non domina le uscite ed e' usata fuori): " << I << "\n";
                            continue;
                        } else {
                            errs() << " [*] Non domina le uscite, ma e' dead fuori dal loop. Procedo!\n";
                        }
                    }

                        // Condizione B: La definizione domina tutti i blocchi in cui viene usata nel loop
                        bool DominatesAllUses = true;
                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                                // Ci interessano solo gli usi interni al loop
                                if (L->contains(UserInst->getParent())) {
                                    // Controllo speciale per i nodi PHI: la dominanza va verificata sul blocco precedente
                                    if (PHINode *PN = dyn_cast<PHINode>(UserInst)) {
                                        // Verifica se domina il blocco antecedente corrispondente all'operando
                                        for (unsigned unsigned_op = 0; unsigned_op < PN->getNumIncomingValues(); ++unsigned_op) {
                                            if (PN->getIncomingValue(unsigned_op) == &I) {
                                                if (!DT.dominates(I.getParent(), PN->getIncomingBlock(unsigned_op))) {
                                                    DominatesAllUses = false;
                                                    break;
                                                }
                                            }
                                        }
                                    } else if (!DT.dominates(I.getParent(), UserInst->getParent())) {
                                        DominatesAllUses = false;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!DominatesAllUses) {
                            errs() << " [!] Non si puo' spostare (non domina tutti i suoi usi): " << I << "\n";
                            continue;
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
    return {LLVM_PLUGIN_API_VERSION, "CustomLICMPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "custom-licm") {
                            FPM.addPass(CustomLICMPass());
                            return true;
                        }
                        return false;
                    });
            }};
}