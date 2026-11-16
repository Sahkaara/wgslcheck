# Report di revisione — 16 novembre 2026

## Risultato

Interventi completati sui quattro ambiti concordati. Il progetto compila con MSVC e GCC; tutti i test eseguiti sul codice finale passano. Le modifiche sono locali, senza commit o push.

## 1. Analisi statica

Aggiunto un passaggio di risoluzione dei nomi che esamina inizializzatori, assegnazioni, return, condizioni e rami alternativi, anche non eseguiti. Segnala variabili inesistenti, uso fuori ambito, auto-riferimenti non risolti negli inizializzatori e chiamate a valori non invocabili.

Le firme vengono raccolte prima dei corpi: le chiamate a funzioni dichiarate successivamente restano valide. Sono verificati i conteggi degli argomenti delle funzioni dichiarate, dei costruttori scalari e delle funzioni matematiche implementate. I costruttori vettoriali hanno un controllo statico parziale, completato a runtime. Le altre funzioni ricevono una diagnostica che distingue esplicitamente il caso sconosciuto/non supportato.

Non si tratta ancora di un type checker completo: compatibilità dei tipi, overload, validità dei return e copertura di tutti i percorsi non sono completamente verificati.

## 2. Attributi

Rimosso il salto anticipato dei controlli per le funzioni senza stadio. Ora `@workgroup_size` viene rifiutato anche su una normale funzione helper priva di `@compute`, oltre che sugli altri stadi.

## 3. Numeri e simulazione

I valori conservano il tipo numerico: intero astratto, float astratto, u32, i32 o f32. L'implementazione comune degli operatori viene usata sia dalle espressioni sia dalle assegnazioni composte.

Aggiunti/corretti:

- divisione intera con troncamento verso zero;
- overflow u32/i32 a 32 bit, inclusa moltiplicazione con intermedi interi;
- shift su interi concreti, con propagazione del segno per i32;
- conversioni intero-intero dei bit a 32 bit;
- conversioni float-intero con saturazione e troncamento;
- arrotondamento f32 per conversioni, operazioni e confronti con valori astratti;
- costruttori scalari senza argomento, conversioni booleane;
- conservazione dei tipi ai confini di locali, parametri, ritorni, builtin matematici e componenti vettoriali supportati;
- diagnostiche per overflow di conversione, risultati non finiti, valori astratti fuori dal range esatto e tipi concreti incompatibili nelle operazioni controllate.

Le regole di riferimento sono nella [specifica WGSL](https://www.w3.org/TR/WGSL/), in particolare [conversioni numeriche](https://www.w3.org/TR/WGSL/#floating-point-conversion).

Limiti espliciti: f16 viene rifiutato, non emulato come double. Gli interi astratti sono limitati a |x| <= 2^53 - 1. Non sono completi la concretizzazione WGSL, le conversioni implicite, la distinzione tra espressioni costanti e runtime, l'aritmetica vettoriale e i casi particolari floating point delle GPU. Il simulatore non deve essere usato come riferimento di conformità.

## 4. Documentazione

README e architettura sono stati riallineati: supporto effettivo a struct, blocchi e builtin, assenza di cicli e risorse, singolo file per comando, output di successo, conteggio delle invocazioni, test realmente presenti e workflow CI sui tre sistemi operativi. Eliminata la descrizione di un sistema golden-file inesistente.

## Verifica sul codice finale

- MSVC: build Release riuscita; CTest **5/5** superati.
- GCC: build C++20 con `-O2 -Wall -Wextra -Wpedantic` riuscita per CLI e test, senza diagnostiche del compilatore.
- GCC: suite unitaria superata; check e sim sull'esempio riusciti, con 32 invocazioni per dispatch 2x2x1.
- GCC: script CLI per regressioni semantiche/numeriche e file mancanti superati.
- Regressioni aggiunte per nomi, scope, arità, attributi, conversioni, limiti numerici, operatori e diagnostiche CLI con controllo del codice di uscita.
- `git diff --check`: nessun errore di whitespace.

Queste verifiche sono locali su Windows con due compilatori. La matrice CI Linux/macOS/Windows non è stata eseguita da questa sessione.

## Sviluppi successivi

Il passo successivo più utile è un sistema di tipi coerente per l'intero AST: consentirebbe di verificare conversioni implicite, return, overload e concretizzazione prima dell'esecuzione. Buffer, memoria condivisa, sincronizzazione, cicli e nuove funzioni WGSL sono funzionalità ulteriori, non implementate in questa revisione.
