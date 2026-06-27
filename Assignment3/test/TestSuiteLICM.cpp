//=============================================================================
// TEST SUITE PER LOOP-INVARIANT CODE MOTION (LICM)
//=============================================================================

// TEST 1: Il "Mistero" del While classico
// Obiettivo: Mette alla prova il controllo "L'istruzione è Dead fuori dal loop?"
int test_while_classic(int a, int b, int N) {
    int i = 0;
    int acc = 0;
    
    while (i < N) {
        int inv = a * b;          // INVARIANTE! (Ma non domina l'uscita, vedi teoria sotto)
        acc += inv + i;
        i++;
    }
    return acc;
}

// TEST 2: Il Do-While e la catena di dipendenze
// Obiettivo: Mette alla prova la dominanza diretta e il ciclo 'do-while' interno al Pass
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

// TEST 3: La "Trappola" dell' IF
// Obiettivo: Mette alla prova il fallimento della Code Motion (codice illegale da spostare)
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