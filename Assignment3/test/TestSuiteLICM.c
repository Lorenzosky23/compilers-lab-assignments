
// Produrre Ir senza m2r > clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S -c TestSuiteLICM.c -o TestSuiteLICM.ll
// poi passo m2r >  opt -passes=mem2reg TestSuiteLICM.ll -S -o TestSuiteLICM.ll
// infine eseguire il passo > opt -load-pass-plugin=../build/LoopPass.so -passes="LoopPass" TestSuiteLICM.ll -S -o TestSuiteLICM.optimized.ll
//=============================================================================
// TEST SUITE PER LOOP-INVARIANT CODE MOTION (LICM)
//=============================================================================

// TEST 1: 
// Obiettivo: Mette alla prova il controllo "L'istruzione è Dead fuori dal loop?"
//In un while classico, il blocco base che contiene il corpo del ciclo non domina 
// l'uscita (perché il ciclo potrebbe fare zero iterazioni e saltare direttamente alla fine). 
// Tuttavia, siccome inv è usata solo per calcolare acc dentro il ciclo, 
// è "dead" (morta) all'esterno. Il passo dovrebbe spostarla con successo.
int test_while_classic(int a, int b, int N) {
    int i = 0;
    int acc = 0;
    
    while (i < N) {
        int inv = a * b;          // INVARIANTE! (Ma non domina l'uscita)
        acc += inv + i;
        i++;
    }
    return acc;
}

// TEST 2: I
// Obiettivo: Mette alla prova la dominanza diretta e il ciclo 'do-while' interno al Pass
//Il passo dovrebbe scoprire prima step1, poi ricomincerà il ciclo di ricerca,
//  scoprirà step2 (che ora ha operandi invarianti),e sposterà entrambe mantenendo l'ordine corretto.
int test_dowhile_chain(int x, int y) {
    int i = 0;
    int res = 0;
    
    do {
        int step1 = x + y;        // INVARIANTE di Livello 1
        int step2 = step1 << 2;   // INVARIANTE di Livello 2 (Dipende da step1!)
        res += step2;
        i++;
    } while (i < 50);
    
    return res;
}

// TEST 3: 
// Obiettivo: Mette alla prova il fallimento della Code Motion (codice illegale da spostare)
// ci aspettiamo che il passo identifichi la moltiplicazione (base * moltiplicatore) come invariante 
// e ne calcoli correttamente la "morte" all'esterno del ciclo. 
// Tuttavia, lo spostamento deve fallire all'ultimo filtro di sicurezza (DominatesAllUses). 
// Trovandosi in un ramo condizionale, la definizione dell'istruzione non domina il nodo phi di ricongiungimento a valle. 
// Ci aspettiamo che il passo scarti l'istruzione, lasciandola all'interno del loop per preservare la semantica e la correttezza del programma.
int test_trap_if(int base, int moltiplicatore, int limite, int condizione_esterna) {
    int i = 0;
    int val = 0;
    
    while (i < limite) {
        if (condizione_esterna > 10) {
            val = base * moltiplicatore; // INVARIANTE! Ma è dentro un IF condizionale!
        }
        i++;
    }
    return val; // 'val' viene usata qui fuori! Se venisse spostata nel preheader, 
                // verrebbe calcolata anche quando 'condizione_esterna <= 10' (ERRORE!).
}