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
//LoopPass.so
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

    // V: L'operando che stiamo analizzando. L: Il puntatore al ciclo (Loop) corrente.
    // Invariants: Il set di istruzioni che abbiamo già verificato essere loop-invariant nelle iterazioni precedenti.
    bool isValueInvariant(Value *V, Loop *L, const std::set<Instruction*> &Invariants) {
        
        // Caso 1: Se l'operando è una Costante letterale o un
        // Argomento della funzione, il suo valore è fissato prima che il programma 
        // entri nel loop -> È  invariante.
        if (isa<Constant>(V) || isa<Argument>(V)) {
            return true;
        }

        // Se è un'istruzione, verifichiamo dove è definita
        if (Instruction *I = dyn_cast<Instruction>(V)) {
            // Se è definita fuori dal loop, è invariante
            //I->getParent() restituisce il BasicBlock in cui risiede l'istruzione
            //  che ha generato il nostro operando. Chiamando L->contains(...), chiediamo al loop se quel 
            // blocco si trova al suo interno. 
            // Se non è contenuto nel loop, significa che l'operando è stato calcolato prima di entrarvi. 
            // È quindi invariante, e ritorniamo true.
            if (!L->contains(I->getParent())) {
                return true;
            }
            // Se è definita dentro il Loop, è invariante solo se è già stata marcata come invariante
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
            

            //Dichiara l'insieme (set) che conterrà i puntatori a tutte le istruzioni riconosciute come invarianti. 
            // L'uso di un std::set garantisce che ogni istruzione sia presente una sola volta.
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

                        //Assumiamo che l'istruzione sia invariante finché non dimostriamo il contrario analizzando 
                        // i suoi ingressi.
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
           // SmallVector è una classe  creata da LLVM per essere più veloce del classico std::vector del C++. 
           // allochiamo lo spazio per 4 puntatori direttamente sullo stack, 
           // nel caso il ciclo avesse più uscite verra allocata automaticametne memoria".
            SmallVector<BasicBlock*, 4> ExitBlocks;
            L->getExitBlocks(ExitBlocks);// Recuperiamo tutti i blocchi d'uscita del loop.

            // Usiamo un `vector` ordinato per accodarvi le istruzioni da spostare.
            // Mantenere l'ordine di scoperta sequenziale garantisce di non spostare
            // una dipendenza 'B' nel preheader prima della sua istruzione madre 'A'.

            // Manteniamo l'ordine di dipendenza usando un vettore per lo spostamento
            // contiene i puntatori alle istruzioni candidate
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
                            //il blocco in cui risiede la nostra istruzione (I.getParent()) domina il blocco di uscita ExitBB?
                            if (!DT.dominates(I.getParent(), ExitBB)) {
                                DominatesAllExits = false;
                                break;
                            }
                        }
                        
                        // CONDIZIONE 1 PER LA CODE MOTION:
                        // Il blocco contenente l'istruzione deve dominare tutte le uscite del loop.
                        if (!DominatesAllExits) {
                            errs() << "L'istruzione [" << I
                                << "] NON domina tutte le uscite del loop -> NON SPOSTABILE\n";
                            continue;
                        }

                        // CONDIZIONE 2 PER LA CODE MOTION:
                        // Non devono esserci altre definizioni della variabile nel loop.
                        // Poiché il pass lavora su IR in forma SSA (dopo mem2reg),
                        // ogni Value SSA possiede per definizione una sola definizione.
                        // Non è quindi necessario calcolare esplicitamente altre reaching definitions.

                        // CONDIZIONE 3 PER LA CODE MOTION:
                        // La definizione deve dominare tutti i suoi usi interni al loop.
                        // Usiamo direttamente gli oggetti Use, perché LLVM gestisce correttamente
                        // anche il caso particolare dei PHI node, dove l'uso avviene sull'arco
                        // entrante e non semplicemente nel BasicBlock che contiene il PHI.
                        bool DominatesAllUses = true;

                        for (Use &U : I.uses()) {

                            Instruction *UserInst = dyn_cast<Instruction>(U.getUser());

                            if (!UserInst) {
                                continue;
                            }

                            // Ci interessano solo gli usi interni al loop
                            if (L->contains(UserInst->getParent())) {

                                // Verifica se la definizione I domina questo specifico uso.
                                // Questa versione di dominates() gestisce correttamente anche i PHI.
                                if (!DT.dominates(&I, U)) {
                                    DominatesAllUses = false;
                                    break;
                                }
                            }
                        }

                        if (!DominatesAllUses) {
                            errs() << "L'istruzione [" << I << "] NON domina tutti i suoi usi interni -> NON SPOSTABILE\n";
                            continue;
                        } else {
                            errs() << "L'istruzione [" << I<< "] ha superato tutti i controlli -> IN CODA PER LO SPOSTAMENTO\n";
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

        // Se abbiamo modificato il codice, notifichiamo LLVM che i vecchi passaggi di 
        // analisi CFG potrebbero non essere puliti
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