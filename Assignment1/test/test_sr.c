// test per verificare il funzionamento di sr
int main() {
    int x = 8;

    // caso 1: moltiplicazioni
    int a = x * 2;
    int b = x * 4;
    int c = x * 8;

    // caso 2: divisioni
    int d = x / 2;
    int e = x / 4;

    // caso avanzato: 2^n - 1
    int f = x * 3;
    int g = x * 7;
    int h = x * 15;

    return a + b + c + d + e + f + g + h;
}