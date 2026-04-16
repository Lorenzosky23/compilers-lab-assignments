// test per verificare il funzionamento di multi - instruction
int main() {
    int b = 10;

    int a1 = b + 5;
    int c1 = a1 - 5;

    int a2 = b + 3;
    int c2 = a2 - 3;

    return c1 + c2;
}