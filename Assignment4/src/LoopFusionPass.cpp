//=============================================================================
// Loop Fusion Pass - Middle-End Optimization
//=============================================================================
// Questo passo implementa la Loop Fusion, un'ottimizzazione mirata a migliorare
// la località spaziale e temporale dei dati nella Cache L1.
// L'algoritmo unisce i corpi di due cicli adiacenti con lo stesso numero di
// iterazioni e flusso di controllo equivalente, a patto che non vi siano
// dipendenze a distanza negativa (Hazard RAW) che ne romperebbero la semantica.
//=============================================================================

//1. Generazione dell'IR senza ottimizzazioni 
//clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S test.c -o test.ll

//2. Passaggio in forma SSA (mem2reg)
//opt -passes=mem2reg test.ll -S -o test.m2r.ll

//3. Esecuzione del Loop Fusion
//opt -load-pass-plugin=../build/LoopFusionPass.so -passes="LoopFusionPass" test.m2r.ll -S -o test.optimized.ll

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

//variabile utilizzata per tenere traccia del numero di loop da stampare
int loop_counter; 

namespace {

// Funzione di utilità per stampare i nomi dei blocchi nel debug
void printBlock(std::string s, BasicBlock *BB) {
    outs() << s << ": ";
    if (BB) BB->printAsOperand(outs(), false); //stampa l'identificatore del blocco
    else outs() << "null";
    outs() << "\n";
}

//=============================================================================
// UTILITIES E CUSTOM GUARD EXTRACTOR
//=============================================================================

// dati problemi riscontrati con L->getLoopGuardBranch() in cui falliva se il blocco aveva predecessori multipli usiamo 
// questa funzione  che ispeziona manualmente l'albero per trovare l'IF precedente al loop.
BranchInst* getCustomGuard(Loop *L) {
    //prima faccimao un tentativo con la funzione nativa
    if (BranchInst *Guard = L->getLoopGuardBranch()) return Guard;
    //se non abbiamo successo: 
    BasicBlock *Preheader = L->getLoopPreheader(); // predniamo il preheader del Loop
    if (Preheader && Preheader->getSinglePredecessor()) { // ci assicuriamo che esista e che abbia un solo blocco padre
        BasicBlock *Pred = Preheader->getSinglePredecessor(); //salviamo il predecessore che è il candidato ad essere il blocco gaurdia
        if (BranchInst *BI = dyn_cast<BranchInst>(Pred->getTerminator())) { //Prendi l'ultima istruzione di questo blocco (getTerminator()) e verifichi che sia un'istruzione di salto (BranchInst).
            if (BI->isConditional()) {
                //  Un vero blocco guardia valuta la condizione d'ingresso.
                // Se il blocco contiene Nodi PHI (I Nodi PHI in LLVM servono a unire variabili che provengono da percorsi diversi.), allora NON è una guardia!
                if (!isa<PHINode>(Pred->begin())) {
                    return BI;
                }
            }
        }
    }
    return nullptr;
}

// Ottiene il blocco di Bypass (il ramo preso quando la guardia valuta a Falso)
// quando la condizione valuta a falso la funziona trova il blocco di destinazione
BasicBlock* getBypassBlock(BranchInst *Guard, Loop *L) {
    if (!Guard) return nullptr; // se non c'è una guardia non c'è un bypass
    return (Guard->getSuccessor(1) != L->getLoopPreheader()) ? Guard->getSuccessor(1) : Guard->getSuccessor(0);
    // uno dei successori è il preheader del loop se la condizione è vera, se è falsa allroa l'altro è il bypass block
    //Questo operatore ternario verifica: se il successore 1 non è il preheader, allora 1 è il blocco di bypass. Altrimenti, lo è per forza il successore 0.
}

// Verifica se due istruzioni branch (guardie) valutano la medesima condizione logica
bool areGuardsEqual(BranchInst *G1, BranchInst *G2) {
    if (G1 && G2 && G1->isConditional() && G2->isConditional()) {
        auto *icmp1 = dyn_cast<ICmpInst>(G1->getCondition());
        auto *icmp2 = dyn_cast<ICmpInst>(G2->getCondition());
        return (icmp1 && icmp2 && icmp1->isIdenticalTo(icmp2)); 
    }
    return false;
}

//=============================================================================
// REQUISITI DELLA LOOP FUSION
//=============================================================================

// REQUISITO 1: ADIACENZA E CODICE INTERMEDIO 
//Due loop si dicono adiacenti se il flusso di esecuzione esce dal primo loop 
// e "cade" direttamente verso l'ingresso del secondo loop

bool areAdjacent(Loop &L1, Loop &L2) {// restituisce true se i loop sono adiacenti
    outs() << "1) I loop sono adiacenti?\n";
    
    BranchInst *G1 = getCustomGuard(&L1);// se non ci sono guardie saranno nullptr
    BranchInst *G2 = getCustomGuard(&L2);
    bool blocksAdjacent = false;
    
    if (G1 && G2) {
        // Caso Guarded: il bypass di L1 deve atterrare esattamente sulla guardia di L2
        blocksAdjacent = (getBypassBlock(G1, &L1) == G2->getParent());
    } else if (!G1 && !G2) {
        // Caso Unguarded: l'uscita di L1 deve essere il preheader di L2
        blocksAdjacent = (L1.getExitBlock() == L2.getLoopPreheader());
    }

    printBlock("  L1 exit block", L1.getExitBlock());
    printBlock("  L2 preheader block", L2.getLoopPreheader());

    if (blocksAdjacent) {
        //Crea un puntatore al blocco che fa da ponte tra i due loop. 
        // Usa un operatore ternario: se G2 esiste (caso guarded) prende il blocco che contiene G2, 
        // altrimenti (caso unguarded) prende il preheader di L2.
        BasicBlock *TransitionBlock = G2 ? G2->getParent() : L2.getLoopPreheader();
        
        for (Instruction &I : *TransitionBlock) {
            if (isa<BranchInst>(&I)) break; 
            
            //  Se l'istruzione non è un branch, controlla se può avere effetti collaterali (come chiamare un'altra funzione esterna o alterare lo stato globale)
            // OPPURE se può leggere o scrivere dalla memoria.
            if (I.mayHaveSideEffects() || I.mayReadOrWriteMemory()) {
                outs() << "  -> Fallimento Adiacenza: l'istruzione " << I << " ha side-effects o usa la memoria.\n";
                return false;
            }
            
            for (Value *Op : I.operands()) {
                if (Instruction *Def = dyn_cast<Instruction>(Op)) {
                      // Verifichiamo se l'istruzione che ha generato il valore (Def) si trova FISICAMENTE DENTRO il Loop 1.
                      // Se è così, significa che l'istruzione sul blocco di transizione dipende da un valore che "nasce" durante le iterazioni del ciclo.
                      // Fondere i loop distruggerebbe il blocco di transizione: non potremmo spostare l'istruzione 'I' prima del nuovo loop 
                      // (il valore non esisterebbe ancora) né dentro il loop (verrebbe eseguita troppe volte alterando la logica).
                    if (L1.contains(Def)) {
                        outs() << "  -> Fallimento Adiacenza: trovata dipendenza di registri.\n";
                        // La fusione è insicura.
                        return false; 
                    }
                }
            }
        }
    }
    
    if (blocksAdjacent) outs() << "=> Loop " << loop_counter << " è adiacente a Loop " << loop_counter+1 << "\n";
    else outs() << "=> I Loops NON sono adiacenti \n";
    
    return blocksAdjacent;
}

// REQUISITO 2: TRIP COUNT IDENTICO (Fix Test 5)
// Il Trip Count è il numero di volte in cui un ciclo viene eseguito.
// Per poter fondere due loop, entrambi devono compiere esattamente lo stesso numero di iterazioni.
// Per fare questo controllo, usiamo la ScalarEvolution (SCEV). 
// SCEV analizza come le variabili (come la i di un for) "evolvono" ad ogni giro e cerca di calcolare una formula 
// matematica precisa per capire quante volte il loop tornerà indietro prima di uscire. 
// Questo numero di "ritorni all'inizio" si chiama Backedge-Taken Count.

bool haveSameIteration(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    outs() << "2) Trip Count Check?\n";
    const SCEV *S1 = SE.getBackedgeTakenCount(&L1);
    const SCEV *S2 = SE.getBackedgeTakenCount(&L2);
        //Se SCEV non è in grado di calcolare il numero di iterazioni otteniamo SCEVCouldNotCompute
    if (isa<SCEVCouldNotCompute>(S1) || isa<SCEVCouldNotCompute>(S2)) {
    outs() << "  -> Impossibile calcolare il trip count per uno o entrambi i loop.\n";
        return false; // se non possiamo determinare il numero di iterazioni interrompiamo
    }
    
    outs()  << "  -> BackedgeTakenCount del Loop " << loop_counter   << ": S1: " << *S1 << "\n";
    outs()  << "  -> BackedgeTakenCount del Loop " << loop_counter+1 << ": S2: " << *S2 << "\n";

    bool same = (S1 == S2);
    if (same) outs() << "=> Loop " << loop_counter << " e Loop " << loop_counter+1 << " hanno lo stesso iteration count\n";
    else outs() << "=> Loop " << loop_counter << " e Loop " << loop_counter+1 << " hanno un diverso iteration counts\n";
    
    return same; //true se hanno lo stesso numero di iterazioni
}

// REQUISITO 3: CONTROL FLOW EQUIVALENCE (CFE)
// Prima di unire due loop, dobbiamo essere certi che se il programma esegue il primo loop, è obbligato a eseguire anche il secondo E viceversa.
// L'Equivalenza del Flusso di Controllo (CFE) si ottiene quando si verificano due condizioni: A domina B E B post-domina A. 
bool isControlFlowEquivalent(Loop &L1, Loop &L2, DominatorTree &DT, PostDominatorTree &PDT) {
    outs() << "3) Control Flow Equivalence?\n";
    
    BranchInst *G1 = getCustomGuard(&L1);
    BranchInst *G2 = getCustomGuard(&L2);

    if (G1 && G2) {
        if (!areGuardsEqual(G1, G2)) {
            outs() << "  -> le guardie non sono semanticamente uguali \n";
            return false;
        } else {
            outs() << "  -> Guardie dei Loop " << loop_counter << "," << loop_counter+1 << " sono equivalenti\n";
        }
    }

    //Sceglie il blocco "rappresentante" del Loop 1 e 2 per fare i calcoli matematici. 
    // Usa un operatore ternario: se c'è una guardia G1, il rappresentante è il blocco che contiene la guardia. 
    // Se non c'è guardia (loop incondizionato), il rappresentante è l'Header (l'ingresso) del Loop 1.
    // Se non c'è guardia (Unguarded): Il loop viene sempre eseguito. 
    // L'Header è l'effettivo punto di ingresso obbligatorio, quindi usiamo lui come rappresentante.


    BasicBlock *L1_block = G1 ? G1->getParent() : L1.getHeader();
    BasicBlock *L2_block = G2 ? G2->getParent() : L2.getHeader();

    bool cfe = (DT.dominates(L1_block, L2_block) && PDT.dominates(L2_block, L1_block));
    
    if (cfe) outs() << "=> Loop " << loop_counter << " control flow equivalente con il loop  " << loop_counter+1 << "\n";
    else outs() << "=> Nessuna equivalenza del control flow\n";
    return cfe; //Restituisce true se il flusso di controllo è equivalente, false in caso contrario.
}

// REQUISITO 4: ASSENZA DI DIPENDENZE A DISTANZA NEGATIVA 
//Non possono sussistere dipendenze di distanza negative tra Lj e Lk
// Una dipendenza di distanza negativa tra Lj e Lk (con Lj che precede Lk
// si verifica quando, all'iterazione m, Lk utilizza un valore calcolato da Lj
// in una successiva iterazione m+n (dove n > 0).
bool haveNotNegativeMemoryDependencies(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    outs () << "4) dipendenze negative?\n";
    
    for (BasicBlock* BB : L1.blocks()) {
        for (Instruction &I : *BB) {
            //Cerca le istruzioni GetElementPtrInst (GEP). In LLVM, il GEP è l'istruzione che calcola gli indirizzi di memoria (come fare Array[i]). 
            // Se l'istruzione non è un GEP, passa alla successiva (continue).
            auto *storeGEP = dyn_cast<GetElementPtrInst>(&I);
            if (!storeGEP) continue;

            //Verifica se l'istruzione subito successiva al GEP è uno StoreInst
            auto *storeInst = dyn_cast<StoreInst>(storeGEP->getNextNode()); 
            if (!storeInst) continue; 

            //prende il puntatore base dell'array usato dal GEP e inizia a cercare tutti gli altri posti nel programma in cui viene usato.
            for (auto &U : storeGEP->getPointerOperand()->uses()) {
                Instruction* user = dyn_cast<Instruction>(U.getUser());
                //Se chi sta usando quell'array non è un'istruzione, o se si trova fuori dal Loop 2, lo ignora
                if (!user || !L2.contains(user)) continue; 
                //Verifica che l'uso dentro L2 sia anch'esso un calcolo di indirizzi (GEP). Ora abbiamo i due contendenti: storeGEP (dove scrive L1) e storeOrLoadGEP (dove legge/scrive L2).
                auto *storeOrLoadGEP = dyn_cast<GetElementPtrInst>(user);
                if (!storeOrLoadGEP) continue;

           
                outs() << "   Istruzione di Store che segue il GEP: " << *storeGEP << "\n";
                outs() << "   Istruzione di Load che segue il secondo GEP:  " << *storeOrLoadGEP << "\n";

                //Chiede a SCEV di calcolare le equazioni che descrivono come cambiano questi due indirizzi di memoria durante i rispettivi cicli.
                const SCEV *storeSCEV = SE.getSCEVAtScope(storeGEP, &L1);
                //Calcola matematicamente la Distanza: sottrae l'equazione dell'indirizzo di L1 da quella di L2 (Diff = L2 - L1).
                const SCEV *storeOrLoadSCEV = SE.getSCEVAtScope(storeOrLoadGEP, &L2);
                
                if (isa<SCEVCouldNotCompute>(storeSCEV) || isa<SCEVCouldNotCompute>(storeOrLoadSCEV)) continue;

                const SCEV *Diff = SE.getMinusSCEV(storeOrLoadSCEV, storeSCEV);

                outs() << "   SCEV Normalizzata Store: " << *storeSCEV << "\n"; 
                outs() << "   SCEV Normalizzata Load:  " << *storeOrLoadSCEV << "\n";
                outs() << "   Differenza SCEV Diff:  " << *Diff << "\n";

                const SCEV *temp = Diff; 
                const SCEVConstant *ConstDiff = dyn_cast<SCEVConstant>(temp);
                
                // Discesa Type-Safe nell'albero SCEV per LLVM 19
                // 1. RICERCA DELL'OFFSET (Distanza iniziale tra gli indici)
                while (temp && !ConstDiff) {
                    if (const auto *NAry = dyn_cast<SCEVNAryExpr>(temp)) temp = NAry->getOperand(0);
                    else if (const auto *Cast = dyn_cast<SCEVCastExpr>(temp)) temp = Cast->getOperand(0);
                    else break; 
                    ConstDiff = dyn_cast<SCEVConstant>(temp);
                }
                
                if (!ConstDiff) continue;

                int offset = ConstDiff->getValue()->getSExtValue();
                outs() << "   Offset: " << offset << "\n";
                
                // 2. RICERCA DELLO STEP (Di quanto si sposta l'indice a ogni iterazione)
                const SCEVAddRecExpr *DiffRec = dyn_cast<SCEVAddRecExpr>(Diff);
                if (!DiffRec) continue;

                const SCEVConstant *ConstStep = dyn_cast<SCEVConstant>(DiffRec->getStepRecurrence(SE));
                if (!ConstStep) continue;

                int step = ConstStep->getValue()->getSExtValue();
                outs() << "   Step : " << step << "\n";
                

                // Se lo step e l'offset vanno nella stessa direzione (entrambi positivi o entrambi negativi),
                // significa che L2 sta cercando di leggere un dato (es. Array[i+1]) che L1 scriverà solo "nel futuro" 
                // (all'iterazione successiva). Questo romperebbe il programma
                if ((step > 0 && offset > 0) || (step < 0 && offset < 0)) {
                    outs() << "-> Dipendenza negativa trovata a causa dell'offset " << offset << " con passo " << step << "\n";
                    return false; 
                }
            }  
        } 
    }
   
    outs() << "=> I cicli " << loop_counter << " e " << loop_counter+1 << " non hanno dipendenze negative \n";
    return true;
}

// Validatore Principale
bool isLoopFusionValid(Loop *L1, Loop *L2, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE) {
    BranchInst *G1 = getCustomGuard(L1);
    BranchInst *G2 = getCustomGuard(L2);
    
    outs() << "0) I Loop sono guarded?\n";
    if ((G1 != nullptr) == (G2 != nullptr)) {
        outs() << "=> I cicli " << loop_counter << " e " << loop_counter+1 << " sono entrambi ugualmente protetti dal guard: " << (G1 != nullptr) << "\n";

    } else {
        outs() << "=> Guard mismatch\n";
        return false;
    }

    if (!areAdjacent(*L1, *L2)) return false;
    if (!haveSameIteration(*L1, *L2, SE)) return false;
    if (!isControlFlowEquivalent(*L1, *L2, DT, PDT)) return false;
    if (!haveNotNegativeMemoryDependencies(*L1, *L2, SE)) return false;
    
    return true;
}

//=============================================================================
// TRASFORMAZIONE FISICA (Topology Rewiring)
//=============================================================================
void mergeLoops(Loop *L1, Loop *L2, Function &F) {

    //Prima di operare, il codice identifica e salva in variabili tutti i "pezzi" fondamentali dei due loop.
    //1. Mappatura Anatomica dei Loop

    //Recupera le istruzioni di guardia e i blocchi che le contengono.
    BranchInst *guard1Inst = getCustomGuard(L1);
    BranchInst *guard2Inst = getCustomGuard(L2);

    BasicBlock *guardL1 = guard1Inst ? guard1Inst->getParent() : nullptr;
    BasicBlock *preHeaderL1 = L1->getLoopPreheader();
    BasicBlock *headerL1 = L1->getHeader();
    BasicBlock *latchL1 = L1->getLoopLatch();
    BasicBlock *firstBlockBodyL1 = headerL1->getTerminator()->getSuccessor(0);
    BasicBlock *lastBlockBodyL1 = latchL1->getSinglePredecessor();
    BasicBlock *exitingL1 = L1->getExitingBlock();

    BasicBlock *guardL2 = guard2Inst ? guard2Inst->getParent() : nullptr;
    BasicBlock *preHeaderL2 = L2->getLoopPreheader();
    BasicBlock *headerL2 = L2->getHeader();
    BasicBlock *latchL2 = L2->getLoopLatch();
    BasicBlock *firstBlockBodyL2 = headerL2->getTerminator()->getSuccessor(0);
    BasicBlock *lastBlockBodyL2 = latchL2->getSinglePredecessor();
    BasicBlock *exitL2 = L2->getExitBlock();

    outs() << "\n*** L1 BLOCKS ***\n";
    printBlock("L1 PreHeader", preHeaderL1);
    printBlock("L1 Header", headerL1);
    printBlock("L1 First Block Body", firstBlockBodyL1);
    printBlock("L1 Last Block Body", lastBlockBodyL1);
    printBlock("L1 Latch", latchL1);
    printBlock("L1 Exiting Block", exitingL1);

    outs() << "*** L2 BLOCKS ***\n";
    printBlock("L2 PreHeader", preHeaderL2);
    printBlock("L2 Header", headerL2);
    printBlock("L2 First Block Body", firstBlockBodyL2);
    printBlock("L2 Last Block Body", lastBlockBodyL2);
    printBlock("L2 Latch", latchL2);
    printBlock("L2 Exit Block", exitL2);

    //2. Unificazione delle Variabili di Induzione

    //Cerca di recuperare la variabile di induzione
    PHINode *inductionVariableL1 = L1->getCanonicalInductionVariable();
    PHINode *inductionVariableL2 = L2->getCanonicalInductionVariable();
    
    if (inductionVariableL1 && inductionVariableL2) {
        outs() << "Induction Variable L1: " << *inductionVariableL1 << "\n";
        outs() << "Induction Variable L2: " << *inductionVariableL2 << "\n";

        //Trova tutte le istruzioni nel Loop 2 che usavano il contatore di L2, e sostituisce con il contatore di L1
        inductionVariableL2->replaceAllUsesWith(inductionVariableL1);
        inductionVariableL2->eraseFromParent(); 
    }


    //3. Rewiring delle Guardie
    if (guardL1 && guardL2) {
        guard1Inst->setSuccessor(1, getBypassBlock(guard2Inst, L2)); 

        std::vector<Instruction*> instGuardL2toMove;
        for (Instruction &inst : *guardL2) {
            if (&inst != guard2Inst) instGuardL2toMove.push_back(&inst);
        }
        for (Instruction *inst : instGuardL2toMove) {
            if (isa<PHINode>(inst)) inst->moveBefore(getBypassBlock(guard2Inst, L2)->getFirstNonPHI());
            else inst->moveBefore(guard1Inst);
        }
    }
    //4. Svuotamento e Fusione di Preheader, Latch e Header
    //Raccoglie tutte le istruzioni dal Preheader di L2.
    std::vector<Instruction*> instPreHeaderL2toMove;
    for (Instruction &inst : *preHeaderL2) {
        //Le sposta dentro il Preheader di L1 (subito prima del salto finale). Ora L1 inizializza i dati per entrambi i loop.
        if (&inst != preHeaderL2->getTerminator()) instPreHeaderL2toMove.push_back(&inst);
    }
    for (Instruction *inst : instPreHeaderL2toMove) {
        outs () << "Istruzione da spostare dal PreheaderL2: " << *inst << "\n";
        inst->moveBefore(preHeaderL1->getTerminator());
    }
    preHeaderL2->replaceSuccessorsPhiUsesWith(preHeaderL1);
  
    latchL2->replaceSuccessorsPhiUsesWith(latchL1);
    //Come fatto per il preheader, raccoglie le istruzioni dall'Header di L2 e le sposta nell'Header di L1.
    std::vector<Instruction*> instHeaderL2ToMove;
    for (Instruction &inst : *headerL2) {
        if (&inst != headerL2->getTerminator()) instHeaderL2ToMove.push_back(&inst);
    }
    for (Instruction *inst : instHeaderL2ToMove) {
        outs() << "sposto l'istruzione dal header di L2: " << *inst << "\n";
        if (isa<PHINode>(inst)) inst->moveBefore(headerL1->getFirstNonPHI());
        else inst->moveBefore(headerL1->getTerminator());
    }


    //Quando il Loop 1 finisce le sue iterazioni, ma salta direttamente all'uscita finale del Loop 2
    exitingL1->getTerminator()->setSuccessor(1, exitL2);
    //L'ultima istruzione del corpo di L1 non salta più al suo latch per ricominciare il giro, ma cade direttamente nel primo blocco del corpo di L2
    lastBlockBodyL1->getTerminator()->setSuccessor(0, firstBlockBodyL2);
    //Quando il corpo di L2 finisce, non va al latch di L2, ma torna al Latch del Loop 1. Il Latch di L1 incrementerà il contatore e riavvierà il ciclo dall'Header di L1.
    lastBlockBodyL2->getTerminator()->setSuccessor(0, latchL1);

    //Questa funzione di utilità nativa di LLVM scansiona la funzione F, 
    // trova i blocchi orfani e irraggiungibili e li distrugge definitivamente, pulendo il codice generato.
    EliminateUnreachableBlocks(F);  
}

//=============================================================================
// DRIVER DEL PASSO
//=============================================================================
struct LoopFusionPass : public PassInfoMixin<LoopFusionPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
        PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
        ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
        
        if (LI.empty()) {
            outs() << "Funzione " << F.getName() << ": Nessun loop trovato nella funzione. \n";
            return PreservedAnalyses::all();
        }

        outs() << "\n   *** LoopFusionPass *** \n";
        outs() << "=== Funzione: " << F.getName() << " === \n\n";

        loop_counter = 1;  
        //andiamo dal basso verso l'alto 
        auto L1_it = LI.rbegin();

        //se la lista inversa è vuota o invalida, esci
        //rend sarebbe reverse end.
        if (L1_it == LI.rend()) return PreservedAnalyses::all();
        //Imposta il secondo iteratore (L2) sul loop esattamente successivo a L1 nell'ordine inverso (che nel codice originale 
        // sarebbe il loop che lo precede). Questo prepara la coppia da analizzare.
        auto L2_it = std::next(L1_it);
        

        //finché c'è un secondo loop da confrontare, continua a girare.
        while (L2_it != LI.rend()) {
            Loop *L1 = *L1_it;
            Loop *L2 = *L2_it;

            outs() << "* controllo Loop " << loop_counter << " e Loop " << loop_counter+1 << " *\n";

            if (isLoopFusionValid(L1, L2, DT, PDT, SE)) {
                outs() << "\nLoop " << loop_counter << " e Loop " << loop_counter+1 << " possono essere fusi\n";
                
                mergeLoops(L1, L2, F);
                
                DT.recalculate(F);  
                PDT.recalculate(F);
                SE.forgetLoop(L1); 
                LI.releaseMemory(); 
                LI.analyze(DT);

                L1_it = LI.rbegin();  
                for(int i = 1; i < loop_counter; i++) L1_it++; 
                L2_it = std::next(L1_it);

                outs() << "\nCicli fusi - L2 è rimosso e L1 è aggiornato. L3 è il nuovo L2 \n\n";
            } else {
                //Se i loop NON potevano essere fusi
                //avanza gli iteratori. Il vecchio L2 diventa il nuovo L1, e prende il successivo. 
                loop_counter++;
                L1_it++;
                L2_it = std::next(L1_it);

                
                outs() << "\n";
            }
        }
        return PreservedAnalyses::none();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusionPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "LoopFusionPass") { 
                            FPM.addPass(LoopFusionPass());
                            return true;
                        }
                        return false;
                    });
            }};
}