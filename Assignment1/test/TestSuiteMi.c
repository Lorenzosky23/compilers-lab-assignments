// ==============================================================================
// PARTE 1: I CASI FELICI (Il passo deve ridurli a "return b;")
// ==============================================================================

int test_01_add_sub_normale(int b) {
    int a = b + 5;
    int c = a - 5;
    return c;
}

int test_02_add_sub_commutato(int b) {
    int a = 5 + b;
    int c = a - 5;
    return c;
}

int test_03_sub_add_normale(int b) {
    int a = b - 10;
    int c = a + 10;
    return c;
}

int test_04_sub_add_commutato(int b) {
    int a = b - 10;
    int c = 10 + a;
    return c;
}

int test_05_mul_div_normale(int b) {
    int a = b * 8;
    int c = a / 8;
    return c;
}

int test_06_mul_div_commutato(int b) {
    int a = 8 * b;
    int c = a / 8;
    return c;
}


// ==============================================================================
// PARTE 2: I CASI TRAPPOLA (Il passo NON deve toccare nulla)
// ==============================================================================

// Trappola 1: Costanti diverse
int test_07_falso_costanti_diverse(int b) {
    int a = b + 5;
    int c = a - 4;
    return c;
}

// Trappola 2: Ordine della sottrazione sballato (10 - (b+10) fa -b)
int test_08_falso_ordine_sottrazione(int b) {
    int a = b + 10;
    int c = 10 - a;
    return c;
}

// Trappola 3: Divisione per zero 
int test_09_falso_div_zero(int b) {
    int a = b * 0;
    int c = a / 0; 
    return c;
}

// Trappola 4: Troncamento intero ( (b/2)*2 non fa b )
int test_10_falso_div_mul(int b) {
    int a = b / 2;
    int c = a * 2;
    return c;
}