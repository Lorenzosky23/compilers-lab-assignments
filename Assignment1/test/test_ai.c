// test che verifica l'algebraic identity

int main() {

    int x = 10;

    int a = x + 0;
    int b = 0 + x;
    int c = x * 1;
    int d = 1 * x;
    
    return a + b + c + d;
}