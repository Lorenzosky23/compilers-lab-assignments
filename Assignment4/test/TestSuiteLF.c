//=============================================================================
// FILE: test_loop_fusion.c
//=============================================================================
// COMANDO PER TESTARE IL PASSO DA TERMINALE:
//
// 1. Genera l'IR grezzo:
//    clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S TestSuiteLF.c -o TestSuiteLF.ll
//
// 2. 
//   opt -passes=mem2reg TestSuiteLF.ll -S -o TestSuiteLF.m2r.ll
//
// 3. Lancia il tuo passo 
//   opt -load-pass-plugin=../build/LoopFusionPass.so -passes="LoopFusionPass" TestSuiteLF.m2r.ll -S -o TestSuiteLF.optimized.ll
//=============================================================================

extern void side_effect_print(); // Funzione esterna fittizia per i test di disturbo


//=============================================================================
// TEST 1: (Unguarded Basic Fusion)
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
// TEST 2: (Guarded Loops CFE)
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
// TEST 3: (Dipendenza Negativa)
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
// TEST 4: (Violazione Adiacenza Strict)
//=============================================================================
// OBIETTIVO VALUTATO:
//    Verifica del blocco di transizione (Requisito 4). Tra l'uscita del primo
//    ciclo e l'ingresso del secondo è presente una chiamata a funzione esterna
//    con potenziali effetti collaterali (Side Effects).
//
// RISULTATO ATTESO: [ FUSIONE ABORTITA ]
//    Non potendo garantire che la funzione esterna non legga o modifiichi gli
//    array A e B, il compilatore non può applicare la Code Motion per spostarla.
//    L'adiacenza fallisce e i due loop rimangono rigorosamente separati.
//=============================================================================
void test_04_trap_intervening_code(int *A, int *B) {
    for (int i = 0; i < 100; i++) {
        A[i] = 1;
    }
    
    side_effect_print(); // <--- DISTURBATORE! Rompe la contiguità del CFG.
    
    for (int j = 0; j < 100; j++) {
        B[j] = 2;
    }
}


//=============================================================================
// TEST 5:  (Iterazioni Asimmetriche)
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