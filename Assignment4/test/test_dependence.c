//=============================================================================
// TEST SUITE PER LOOP FUSION: DIPENDENZE DI MEMORIA
//=============================================================================
//clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S test_dependence.c -o test_dependence.ll
//opt -passes=mem2reg test_dependence.ll -S -o test_dependence.m2r.ll
//opt -load-pass-plugin=../build/LoopFusionPass.so -passes="LoopFusionPass" test_dependence.m2r.ll -S -o test_dependence.optimized.ll

// CASO 1: Dipendenza Negativa (FUSIONE DA BLOCCARE)
// Come descritto nelle slide, L1 calcola tutti i valori di A[i]. 
// L2 cerca di leggere A[i+3]. Se fondessimo i loop, all'iterazione i=0, 
// L2 cercherebbe di leggere A[3] prima che L1 lo abbia calcolato all'iterazione i=3.
void test_unsafe_fusion(int *A, int *B, int N) {
    // Loop 1 (L1)
    for (int i = 0; i < N; i++) {
        A[i] = i * 10;
    }

    // Loop 2 (L2) - Legge nel "futuro"
    for (int i = 0; i < N; i++) {
        B[i] = A[i + 3] + 5;
    }
}

// CASO 2: Dipendenza Sicura (FUSIONE DA AUTORIZZARE)
// L1 calcola A[i]. L2 legge A[i]. 
// Se fondiamo i loop, all'iterazione i=0, L1 calcola A[0] e subito dopo
// L2 legge A[0]. L'ordine logico è rispettato, nessuna dipendenza negativa.
void test_safe_fusion(int *A, int *B, int N) {
    // Loop 1 (L1)
    for (int i = 0; i < N; i++) {
        A[i] = i * 10;
    }

    // Loop 2 (L2) - Legge nel presente
    for (int i = 0; i < N; i++) {
        B[i] = A[i] + 5;
    }
}