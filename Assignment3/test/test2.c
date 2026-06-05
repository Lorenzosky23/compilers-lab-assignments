#include <stdio.h>

void test_licm(int n, int a, int b) {
    int x = 0;
    for (int i = 0; i < n; i++) {
        // Istruzioni Loop-Invariant 
        int invariant1 = a + b; 
        int invariant2 = invariant1 * 2;
        
        // Istruzione NON invariante
        x += invariant2 + i; 
    }
    printf("%d\n", x);
}

int main() {
    test_licm(10, 5, 3);
    return 0;
} 