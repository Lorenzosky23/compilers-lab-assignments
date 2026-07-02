# compilers-lab-assignments

Questo repository contiene i progetti sviluppati per il laboratorio del corso di Compilatori.

## Struttura del Repository

Il progetto è suddiviso in quattro assignment, ognuno mirato a specifiche tecniche di analisi e ottimizzazione del codice:

* **`Assignment1/` - Local Optimizations:** Contiene l'implementazione di ottimizzazioni locali di base (Local Opts). I passi implementati includono:
    * *Strength Reduction*
    * *Algebraic Simplification* (Identity)
    * *Multi Instruction Optimization*
* **`Assignment2/` - Data Flow Analysis:** Contiene l'elaborato teorico (in formato PDF) dedicato all'analisi del flusso dei dati all'interno del Control Flow Graph (CFG).
* **`Assignment3/` - Loop Invariant Code Motion (LICM):** Implementazione del passo di spostamento del codice invariante fuori dai cicli per ottimizzare le prestazioni.
* **`Assignment4/` - Loop Fusion:** Implementazione di un passo avanzato per la fusione di cicli adiacenti, previa verifica delle dipendenze dei dati (Data Dependence Analysis).

Ogni cartella di assignment contenente codice (1, 3 e 4) segue questa gerarchia standard:
* `src/`: Codice sorgente dei passi LLVM.
* `test/`: Suite di test in C e file LLVM IR (`.ll`) per verificare il funzionamento dei passi.
* `build/`: Cartella generata da CMake contenente i Makefile e le librerie condivise compilate (`.so`).

---

##  Requisiti e Compilazione

Per compilare i passi di ottimizzazione, è necessario avere installati `cmake`, `make` e le librerie di sviluppo di LLVM (versione compatibile con il nuovo Pass Manager).

Per compilare un assignment (es. `Assignment4`), entra nella sua cartella e procedi con la generazione della build:

```
cd Assignment4
mkdir -p build
cd build
cmake ..
make
```

Flusso di Lavoro ed Esecuzione
Tutti i comandi seguenti presuppongono che il tuo terminale sia posizionato all'interno della cartella test/ dell'assignment di interesse (es. Assignment4/test/).

1. Generare l'IR non ottimizzato
Per generare il codice LLVM IR di partenza a partire da un file sorgente C, disabilitando le ottimizzazioni di default di Clang:

```
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S nome_test.c -o nome_test.ll
```
2. Promozione della Memoria ai Registri (Fondamentale per i Loop)
Specialmente per Assignment3 (LICM) e Assignment4 (Loop Fusion), l'IR generato direttamente da Clang mantiene le variabili locali in memoria tramite alloca. Per permettere ai passi sui cicli di funzionare correttamente, è necessario promuovere la memoria ai registri utilizzando il passo mem2reg:

```
opt -passes=mem2reg -S nome_test.ll -o nome_test.m2r.ll
```

3. Eseguire il passo di ottimizzazione personalizzato
Ora puoi applicare la tua libreria compilata al file IR (utilizzando l'IR base per A1, o l'IR passato nel mem2reg per A3/A4). Usa il flag -load-pass-plugin puntando alla cartella build/ del livello superiore (../build/) e specifica il nome esatto del passo registrato nel C++.

Esempio per Assignment 1 (es. Algebraic Identity):

```
opt -load-pass-plugin=../build/libAssignment1.so -passes=NOME_DEL_PASSO -S TestSuiteAi.ll -o TestSuiteAi_ottimizzato.ll
```
Esempio per Assignment 4 (Loop Fusion):

```
opt -load-pass-plugin=../build/LoopFusionPass.so -passes=NOME_DEL_PASSO -S TestSuiteLF.m2r.ll -o TestSuiteLF.optimized.ll
```
