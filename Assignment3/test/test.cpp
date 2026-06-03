#include <iostream>

void test_licm(int B, int C) {
    int risultato = 0;
    
    for (int i = 0; i < 100; i++) {
        // Questa istruzione è LOOP-INVARIANT (sarà spostata nel preheader)
        int A = B + C; 
        
        // Questa istruzione NON è invariante (rimarrà dentro il loop)
        risultato += A + i; 
    }
    
    std::cout << "Risultato: " << risultato << std::endl;
}

int main() {
    test_licm(10, 20);
    return 0;
}