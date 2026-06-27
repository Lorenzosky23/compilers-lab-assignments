//=============================================================================
// FILE: LoopPass.cpp
//
// DESCRIZIONE:
//     Passo di ottimizzazione LLVM (Middle-end) che implementa l'analisi dei
//     cicli naturali (Loop Analysis) e una trasformazione di Loop-Invariant 
//     Code Motion (LICM). 
//     Il passo identifica le istruzioni la cui computazione produce sempre lo 
//     stesso risultato all'interno del ciclo e, se le condizioni di dominanza 
//     lo permettono, le sposta nel "preheader" (blocco d'ingresso esterno), 
//     evitando ricalcoli ridondanti ad ogni iterazione.
//=============================================================================
// Runnare il passo con 'opt':
//      [Only logs]:  opt -load-pass-plugin=./libLoopPass.so -passes="LoopPass" -disable-output [irname]
//      [Emit IR]:    opt -load-pass-plugin=./libLoopPass.so -passes="LoopPass" [irname] -S -o out.ll
//=============================================================================


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

//-----------------------------------------------------------------------------
// LoopPass: Struttura principale del Function Pass
// Eredita da PassInfoMixin per integrarsi automaticamente nel New Pass Manager.
//-----------------------------------------------------------------------------
struct LoopPass : public PassInfoMixin<LoopPass> {
    
    // Funzione ausiliaria per verificare se un operando (Value) è "Loop-Invariant"
    // Ritorna 'true' se il valore non cambia mai durante l'esecuzione del ciclo.
    bool isValueInvariant(Value *V, Loop *L, const std::set<Instruction*> &Invariants) {
        
        // Caso 1: Se l'operando è una Costante letterale (es. il numero 42) o un
        // Argomento della funzione, il suo valore è fissato prima che il programma 
        // entri nel loop -> È  invariante.
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
        
        // RECUPERO DELLE ANALISI 
        // 1. LoopAnalysis: Rileva i cicli naturali analizzando i backedge.
        // 2. DominatorTreeAnalysis: Stabilisce le relazioni gerarchiche tra blocchi
        //    (un blocco A "domina" B se ogni percorso d'esecuzione per B deve passare da A).
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F); 
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);


        // Se la funzione corrente non contiene cicli d'esecuzione, usciamo subito.
        if (LI.empty()) {
            errs() << "Nessun Loop trovato in questa funzione.\n";
            return PreservedAnalyses::all(); // Segnaliamo: non abbiamo alterato il codice IR.
        }

       // Traccia se applicheremo almeno una modifica al CFG.
        bool Changed = false;

        // Iteriamo su tutti i loop della funzione
        for (Loop *L : LI) {
            errs() << "\n========================================\n";
            errs() << " Analizzo Loop: " << L->getHeader()->getName() << "\n";

          
            // REQUISITO : FORMA CANONICA SEMPLIFICATA (Loop Simplify Form)
            // Per poter estrarre codice in sicurezza, pretendiamo che il loop abbia 
            // una struttura normalizzata: un singolo blocco di ingresso (preheader),
            // un singolo blocco di header e uscite dedicate.

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
            // Cerchiamo le istruzioni all'interno del loop i cui operandi sono invarianti.
            // Poiché un'istruzione invariante 'B' potrebbe dipendere da una 'A' posta più in alto,
            // usiamo un approccio iterativo a strati (do-while) finché l'insieme smette di crescere.
            
            std::set<Instruction*> InvariantInstructions;
            bool NewInvariantFound;

            // Ripetiamo in loop finché non troviamo più nuove istruzioni invarianti 
            do {
                NewInvariantFound = false;

                for (BasicBlock *BB : L->blocks()) {
                    for (Instruction &I : *BB) {

                    // FILTRI DI SICUREZZA :
                        // 1. isa<PHINode>(&I): Escludiamo i nodi PHI Servono alla forma SSA per 
                        //    unire flussi divergenti all'ingresso dei blocchi. Nei loop gestiscono 
                        //    variabili interattive (come contatori i++) e dipendono dal backedge.
                        //    Spostare fisicamente un PHINode nel preheader distruggerebbe il CFG.
                        // 2. mayHaveSideEffects(): Saltiamo istruzioni che alterano lo stato 
                        //    del processore o del sistema operativo (es. stampe a video, chiamate esterne).
                        // 3. mayReadFromMemory(): Evitiamo le 'load'. Un dato letto da un puntatore
                        //    in memoria potrebbe essere modificato da una 'store' altrove nel loop.
                        // 4. isTerminator(): Saltiamo salti condizionati (br) o istruzioni di return.
                        if (isa<PHINode>(&I) || I.mayHaveSideEffects() || I.mayReadFromMemory() || I.isTerminator()) {
                            continue;
                        }

                        // Se l'abbiamo già promossa a invariante nel ciclo precedente, saltiamo.
                        if (InvariantInstructions.count(&I)) {
                            continue;
                        }

                        // Verifica Data-Flow: Un'istruzione è invariante se e solo se
                        // TUTTI i suoi operandi in input sono a loro volta invarianti.
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
            L->getExitBlocks(ExitBlocks);// Recuperiamo tutti i blocchi d'uscita del loop.

            // Usiamo un `vector` ordinato per accodarvi le istruzioni da spostare.
            // Mantenere l'ordine di scoperta sequenziale garantisce di non spostare
            // una dipendenza 'B' nel preheader prima della sua istruzione madre 'A'.

            // Manteniamo l'ordine di dipendenza usando un vettore per lo spostamento
            std::vector<Instruction*> InstructionsToMove;

            for (BasicBlock *BB : L->blocks()) {
                for (Instruction &I : *BB) {
                    if (InvariantInstructions.count(&I)) {
                        
                        // Verifico se il blocco dell'istruzione domina tutte le uscite del loop
                        // REQUISITO DI DOMINANZA SULLE USCITE:
                        // L'istruzione deve trovarsi in un blocco che "domina" tutte le uscite.
                        // Se così non fosse, spostarla nel
                        // preheader significherebbe farla eseguire SEMPRE, anche quando il loop
                        // avrebbe terminato prima di incontrarla.
                        bool DominatesAllExits = true;
                        for (BasicBlock *ExitBB : ExitBlocks) {
                            if (!DT.dominates(I.getParent(), ExitBB)) {
                                DominatesAllExits = false;
                                break;
                            }
                        }
                        
                        // Se l'istruzione NON domina le uscite, possiamo spostarla ugualmente
                        // a patto che il suo risultato sia "morto" (dead) all'esterno, ossia se
                        // nessun basic block fuori dal loop utilizzerà mai questo registro.
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
                // Il punto d'ancoraggio è il 'Terminator' (l'istruzione di salto finale)
                // del blocco Preheader. Inseriremo le invarianti immediatamente prima di esso.
                Instruction *InsertPos = Preheader->getTerminator(); // Inseriamo subito prima del salto finale del preheader
                
                for (Instruction *I : InstructionsToMove) {
                    errs() << " >>> SPOSTO NEL PREHEADER: " << *I << "\n";

                    //moveBefore() si occupa di
                    // scollegare l'istruzione dal suo vecchio BB e ricollegarla nella nuova
                    // posizione, preservando intatti i riferimenti SSA nella tabella dei simboli.
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

//-----------------------------------------------------------------------------
// REGISTRAZIONE DEL PLUGIN IN 'OPT' 
// Esporta il punto d'aggancio esterno standard per permettere al compilatore
// 'opt' o 'clang' di caricare dinamicamente la libreria condivisa (.so).
//-----------------------------------------------------------------------------
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