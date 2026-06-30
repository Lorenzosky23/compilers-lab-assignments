//=============================================================================
// FILE: test_loop_fusion.c
//
//    Il file raggruppa 5 scenari procedurali: 2 casi positivi 
//    (in cui l'ottimizzazione deve avvenire) e 3 casi trappola (in cui il 
//    compilatore deve salvaguardare la semantica originale abortendo la fusione).
//=============================================================================
// COMANDO PER TESTARE IL PASSO DA TERMINALE:
//
// 1. Genera l'IR grezzo:
//    clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S test_loop_fusion.c -o raw.ll
//
// 2. Esegui la normalizzazione pre-flight (SSA + LoopSimplifyForm):
//    opt -passes="mem2reg,simplifycfg,loop-simplify" raw.ll -S -o clean.ll
//
// 3. Lancia il tuo passo guardando i log di debug:
//    opt -load-pass-plugin=./build/libLoopFusionPass.so -passes="LoopFusionPass" 
//        -debug-only="my-loop-fusion" -disable-output clean.ll
//=============================================================================

extern void side_effect_print(); // Funzione esterna fittizia per i test di disturbo


//=============================================================================
// TEST 1: Il Caso da Manuale (Unguarded Basic Fusion)
//=============================================================================
// OBIETTIVO VALUTATO: 
//    Verifica del percorso ideale. Due cicli contigui, con Trip Count identico 
//    (100 iterazioni), forma semplificata pura e accessi di memoria perfettamente
//    allineati temporalmente (Distanza d = 0).
//
// RISULTATO ATTESO: [ FUSIONE ESEGUITA ]
//    I due corpi "for.body" vengono unificati. Il contatore 'j' viene soppresso
//    e sostituito dalla Induction Variable 'i' del primo ciclo. L'operazione
//    B[i] = A[i] sfrutterà al massimo la cache L1.
//=============================================================================
void test_01_basic_unguarded(int *A, int *B) {
    for (int i = 0; i < 100; i++) {
        A[i] = i * 2;
    }
    for (int j = 0; j < 100; j++) {
        B[j] = A[j] + 10;
    }
}


//=============================================================================
// TEST 2: L'Estensione d'Esame UNIMORE (Guarded Loops CFE)
//=============================================================================
// OBIETTIVO VALUTATO:
//    Mette alla prova la Control Flow Equivalence (CFE) sui cicli "protetti".
//    Nelle architetture reali, i cicli dipendenti da un parametro 'N' vengono
//    preceduti da un If-Guard (N > 0). Il test accerta se il nostro algoritmo
//    riesce a dimostrare l'equivalenza analizzando l'uguaglianza delle guardie.
//
// RISULTATO ATTESO: [ FUSIONE ESEGUITA ]
//    Anche se la Post-Dominanza classica fallisce a causa del salto di bypass,
//    il requisito CFE riconosce che le due guardie testano la stessa variabile 'N'
//    e che il ramo False di L0 atterra sulla guardia di L1. I loop vengono fusi.
//=============================================================================
void test_02_guarded_unimore(int *A, int *B, int N) {
    if (N > 0) {
        for (int i = 0; i < N; i++) {
            A[i] = 5;
        }
    }
    if (N > 0) { // <--- Stessa identica condizione IR!
        for (int j = 0; j < N; j++) {
            B[j] = A[j] * 3;
        }
    }
}


//=============================================================================
// TEST 3: La Trappola della Memoria (Hazard RAW - Dipendenza Negativa)
//=============================================================================
// OBIETTIVO VALUTATO:
//    Mette sotto torchio l'analizzatore algebrico Scalar Evolution (Requisito 5).
//    Il secondo ciclo legge l'elemento A[j + 3], introducendo una dipendenza a
//    distanza negativa (-12 byte). 
//
// RISULTATO ATTESO: [ FUSIONE ABORTITA ]
//    Se il compilatore fondesse i cicli, all'iterazione 0 il codice tenterebbe
//    di leggere A[3], che non è ancora stato calcolato dalla Store (verrebbe 
//    scritto solo al futuro giro 3!). Il passo deve rilevare l'hazard e fermarsi.
//=============================================================================
void test_03_trap_negative_distance(int *A, int *B) {
    for (int i = 0; i < 100; i++) {
        A[i] = i;
    }
    for (int j = 0; j < 100; j++) {
        B[j] = A[j + 3] * 2; // <--- VIOLAZIONE SEMANTICA!
    }
}


//=============================================================================
// REQUISITO 4: ADIACENZA STRETTA (Aggiornato per supportare i Guarded Loops)
//=============================================================================
// SPIEGAZIONE TEORICA:
// L'adiacenza varia a seconda della topologia del ciclo:
// - CASO UNGUARDED (Test 1): L'uscita del Loop 0 sfocia direttamente nel 
//   Preheader del Loop 1.
// - CASO GUARDED (Test 2): La guardia del Loop 0, nel suo ramo "False" (bypass),
//   punta direttamente alla guardia del Loop 1. Inoltre, il corpo del Loop 0 
//   quando termina le iterazioni deve fluire verso la guardia o il preheader di L1.
// In entrambi i casi, non ci devono essere istruzioni pericolose in mezzo.
//=============================================================================
static bool canBeMadeAdjacent(Loop *L0, Loop *L1) {
    BasicBlock *Exit0 = L0->getExitBlock();
    BasicBlock *Preheader1 = L1->getLoopPreheader();

    // Se la struttura di base è corrotta, abortiamo subito
    if (!Exit0 || !Preheader1) return false;

    BranchInst *Guard0 = L0->getLoopGuardBranch();
    BranchInst *Guard1 = L1->getLoopGuardBranch();

    // --- CASO B: GUARDED LOOPS (Test 2) ---
    if (Guard0 && Guard1) {
        BasicBlock *Guard1Block = Guard1->getParent();
        BasicBlock *BypassL0 = nullptr;
        
        // Cerchiamo il ramo "False" della guardia di L0 (quello che non entra nel ciclo)
        for (BasicBlock *Succ : successors(Guard0->getParent())) {
            if (Succ != L0->getLoopPreheader()) {
                BypassL0 = Succ;
                break;
            }
        }

        // Regola teorica: Il bypass di L0 deve atterrare esattamente sulla guardia di L1
        if (BypassL0 == Guard1Block) {
            
            // Dobbiamo assicurarci che anche quando il Loop 0 finisce i suoi giri,
            // l'uscita punti alla guardia del Loop 1 (come accade nel Test 2 con il blocco 13 -> 14)
            BasicBlock *SingleSucc = Exit0->getSingleSuccessor();
            
            if (SingleSucc == Guard1Block || SingleSucc == Preheader1) {
                // Scansione di sicurezza finale sul blocco di transizione
                for (Instruction &I : *Exit0) {
                    if (I.isTerminator()) continue;
                    if (I.mayHaveSideEffects() || I.mayReadOrWriteMemory()) {
                        LLVM_DEBUG(dbgs() << "  [X] Trovate istruzioni con side-effect nell'uscita del ciclo protetto.\n");
                        return false;
                    }
                }
                LLVM_DEBUG(dbgs() << "  [V] Adiacenza Guarded validata strutturalmente!\n");
                return true;
            }
        }
    }

    // --- CASO A: UNGUARDED LOOPS (Test 1) ---
    // Regola teorica: L'exit di L0 è esattamente il preheader di L1
    if (Exit0 == Preheader1) {
        for (Instruction &I : *Exit0) {
            if (I.isTerminator()) continue;
            if (I.mayHaveSideEffects() || I.mayReadOrWriteMemory()) {
                 LLVM_DEBUG(dbgs() << "  [X] Trovate istruzioni non spostabili tra i due cicli puri.\n");
                 return false;
            }
        }
        LLVM_DEBUG(dbgs() << "  [V] Adiacenza Unguarded validata!\n");
        return true;
    }

    LLVM_DEBUG(dbgs() << "  [X] Fallimento Adiacenza: topologia dei blocchi incompatibile.\n");
    return false;
}

//=============================================================================
// TEST 5: La Trappola del Trip Count (Iterazioni Asimmetriche)
//=============================================================================
// OBIETTIVO VALUTATO:
//    Mette alla prova il calcolo del "Backedge Taken Count" di SCEV (Requisito 3).
//    Il primo ciclo ruota 100 volte, il secondo solo 50.
//
// RISULTATO ATTESO: [ FUSIONE ABORTITA ]
//    La fusione di due loop richiede la perfetta sincronia dei contatori. Unificare
//    i corpi provocherebbe l'esecuzione di 50 scritture su B[] totalmente fuori
//    specie o buffer overflow. Il passo scarta la coppia istantaneamente.
//=============================================================================
void test_05_trap_different_tripcount(int *A, int *B) {
    for (int i = 0; i < 100; i++) {
        A[i] = i;
    }
    for (int j = 0; j < 50; j++) { // <--- 50 != 100
        B[j] = j;
    }
}