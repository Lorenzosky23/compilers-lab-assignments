
// Produrre Ir senza m2r > clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S -c TestSuiteLICM.c -o TestSuiteLICM.ll
// poi passo m2r >  opt -passes=mem2reg TestSuiteLICM.ll -S -o TestSuiteLICM.ll
// infine eseguire il passo > opt -load-pass-plugin=../build/LoopPass.so -passes="LoopPass" TestSuiteLICM.ll -S -o TestSuiteLICM.optimized.ll
//=============================================================================
// TEST SUITE PER LOOP-INVARIANT CODE MOTION (LICM)
//=============================================================================

// TEST 1: 
// Scopo: Verificare la distinazionr fra "istruzioni Loop Invariant " e "Istruzioni spostabili"
// Istruzione da analizzare: int inv = a*b
// - loop invariant: 'a' e 'b' sono parametri della funzione, quindi definiti fuori dal loop
// - controllo code motion: il blocco contenente 'a*b' deve dominare tutte le uscite ma in un while
//   deve essere sempre verificata la condizone: se nella prima iterazione la condizione è falsa allora 
//   non viene mai eseguito
// Output: Trovata istruzione loop invariant a*b, non domina tutte le uscite --> NON SPOSTABILE
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

// TEST 2: 
// Scopo: Verificare che il pass riesca a riconoscere anche istruzioni invarianti che dipendono da altre istruzioni
// già riconosciute come invarianti
// Istruzioni da analizzare: step = x + y , step2 = step << 2 
// STEP --> loop invariant: si perchè 'x' e 'y' sono parametri della funzione 
// STEP2 --> loop invariant: si perchè step1 è definito dentro la funzione ma è loop invariant, '2' è costante
// Output: Spostabili entrambe 
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
// Scopo: Verificare che un'istruzione possa essere riconosciuta come loop invariant ma essere scartata durante la code motion
// Istruzioni: condizione_esterna>10 (diventa icmp in LLVM), val = base * moltiplicatore
// Loop invariant --> entrambe: dipendono dai parametri della funzione
// Code motion --> sono all'interno di un percorso condizionale, quindi non dominano tutte le uscite
// Output --> Non spostabile per entrambe
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