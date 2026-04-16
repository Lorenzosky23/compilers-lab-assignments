// testa che tutte e 3 le ottimizzazioni funzionino insieme (interagendo)
int main() {
    int x = 5;

    int a = x + 0;     // ottimizzazione ai
    int b = a * 8;     // ottimizzazione sr
    int c = b * 15;    // ottimizzazione sr avanzato

    int d = x + 4;
    int e = d - 4;     // ottimizzazione mi

    return c + e;
}