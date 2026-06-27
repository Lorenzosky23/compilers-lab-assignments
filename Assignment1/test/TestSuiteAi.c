/*
 * ============================================================================
 * TEST SUITE: ALGEBRAIC IDENTITY (AI) - Completa 6 Casi
 * ============================================================================
 * Obiettivo: Verificare l'elisione delle identità matematiche elementari
 * implementate nel pass: (x + 0), (0 + x), (x * 1), (1 * x), (x - 0), (x / 1).
 */

// ==========================================
// TEST 1: Identità Addizione (x + 0)
// IR Attesa: L'istruzione 'add' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_add_zero_r(int a) {
    return a + 0;
}

// ==========================================
// TEST 2: Identità Addizione Commutata (0 + x)
// IR Attesa: L'istruzione 'add' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_add_zero_l(int a) {
    return 0 + a;
}

// ==========================================
// TEST 3: Identità Moltiplicazione (x * 1)
// IR Attesa: L'istruzione 'mul' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_mul_one_r(int a) {
    return a * 1;
}

// ==========================================
// TEST 4: Identità Moltiplicazione Commutata (1 * x)
// IR Attesa: L'istruzione 'mul' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_mul_one_l(int a) {
    return 1 * a;
}

// ==========================================
// TEST 5: Identità Sottrazione (x - 0)
// IR Attesa: L'istruzione 'sub' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_sub_zero_r(int a) {
    return a - 0;
}

// ==========================================
// TEST 6: Identità Divisione (x / 1)
// IR Attesa: L'istruzione 'sdiv' viene cancellata. Ritorna il parametro %0.
// ==========================================
int test_div_one_r(int a) {
    return a / 1;
}


/*
 * ============================================================================
 * TEST DI CONTROLLO E TRAPPOLE (Anti-Falsi Positivi)
 * Dimostrano che il pass ignora le operazioni non neutre e rispetta i segni.
 * ============================================================================
 */

// TEST 7: Addizione con costante attiva (x + 5)
// IR Attesa: NESSUNA MODIFICA. L'istruzione 'add ... 5' deve rimanere intatta.
int test_controllo_add(int a) {
    return a + 5;
}

// TEST 8: Moltiplicazione con costante attiva (x * 3)
// IR Attesa: NESSUNA MODIFICA. L'istruzione 'mul ... 3' deve rimanere intatta.
int test_controllo_mul(int a) {
    return a * 3;
}

// TEST 9: TRAPPOLA - Sottrazione da zero (0 - x)
// IR Attesa: NESSUNA MODIFICA. Il risultato è -x, non x. L'istruzione 'sub' deve restare.
int test_controllo_sub_inversa(int a) {
    return 0 - a;
}

// TEST 10: TRAPPOLA - Divisione con dividendo fisso (1 / x)
// IR Attesa: NESSUNA MODIFICA. L'istruzione 'sdiv 1, %0' deve restare intatta.
int test_controllo_div_inversa(int a) {
    return 1 / a;
}