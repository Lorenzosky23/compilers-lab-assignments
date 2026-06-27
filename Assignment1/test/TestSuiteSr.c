// === CASI POSITIVI (Devono essere ottimizzati) ===

// Test 1: Moltiplicazione base (potenza di 2)
int test_mul_base(int b) {
    return b * 8; 
}

// Test 2: Moltiplicazione commutata (Costante a sinistra)
int test_mul_commutato(int b) {
    return 15 * b; 
}

// Test 3: Divisione con segno (SDiv)
int test_sdiv(int b) {
    return b / 4; 
}

// Test 4: Divisione senza segno (UDiv)
unsigned int test_udiv(unsigned int b) {
    return b / 16;
}

// === CASI NEGATIVI / TRAPPOLE (Non devono essere toccati) ===

// Test 5: Moltiplicazione non ottimizzabile (13 non è 2^n né 2^n-1)
int test_no_ottimizzazione(int b) {
    return b * 13;
}

// Test 6: Divisione con costante a sinistra (non ottimizzabile)
int test_div_inverso(int b) {
    return 8 / b; 
}