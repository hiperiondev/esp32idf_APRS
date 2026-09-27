# Test di regressione sull'host — trasmettitore HDLC

🇬🇧 [English](README.md) · 🇪🇸 [Español](README.es.md) · 🇮🇹 Italiano

`test_ax25_tx_hdlc.c` compila i sorgenti reali `src/ax25.c`, `src/crc_ccit.c` e
quelli di FX.25 (`src/fx25.c`, `lwfec/rs.c`, `lwfec/gf.c`) per l'host, con gli
header ESP-IDF sostitutivi di `stubs/`, sotto AddressSanitizer e
UndefinedBehaviorSanitizer.

Invia trame UI AX.25 casuali attraverso `Ax25WriteTxFrame()` →
`Ax25TransmitCheck()` → `Ax25GetTxBit()` e verifica ogni trasmissione:

- bit per bit contro un codificatore HDLC di riferimento (trama + FCS riempite
  come un unico campo, compreso lo 0 di riempimento dovuto quando una sequenza di
  cinque 1 termina sull'ultimo bit della FCS);
- con il ricevitore interno (`Ax25BitParse()` / `Ax25ReadNextRxFrame()`);
- con un ricevitore rigoroso che segue le regole di `hdlc_rec.c` di Direwolf e
  richiede esattamente sette bit di dati al flag di chiusura.

Un secondo passaggio invia trame come blocchi FX.25 attraverso il ricevitore
interno. Mentre `Ax25GetTxBit()` viene cadenzata, le pagine che contengono
`Fx25ModeList` vengono rese inaccessibili con `mprotect()`, al posto della cache
della flash disabilitata sotto la ISR del DAC sicura rispetto alla cache: una
lettura della tabella dal percorso dei bit in trasmissione termina l'esecuzione
con un errore di segmentazione.

Un terzo passaggio costruisce trame con 0-8 digipeater da testo TNC2 tramite
`ax25_encode()` e `hdlcFrame()` e verifica che solo l'ultimo indirizzo porti il
bit di fine indirizzo, che `ax25_decode()` recuperi tutti i digipeater e che la
trama superi una trasmissione e il ricevitore interno.

Un quarto passaggio gira in half duplex e misura quanto attende ogni trama in
coda prima di trasmettere: con `Ax25TimeSlot(0)` ogni trama parte subito, anche
se prima era stato impostato uno slot diverso da zero; con uno slot di 2000 ms
ogni trama attende almeno 2000 ms.

Un quinto passaggio verifica la fine di un'attivazione e il cancello di
attivazione: per la coda predefinita e diverse lunghezze di `Ax25TxTail()` ogni
trama deve essere seguita esattamente da un flag di chiusura più la coda
arrotondata per eccesso a flag interi, e una trama in coda non deve attivarsi
finché `getTransmit()` è vero o lo smontaggio dello spegnimento precedente è
ancora dovuto; poi deve attivarsi appena entrambi si liberano.

Il rapporto indica quante trame hanno chiuso la FCS su cinque 1, quindi un PASS
copre sempre quel caso. L'esecuzione richiede un host POSIX (`mprotect()`,
`sysconf()`).

```bash
make              # compila ed esegui, seme predefinito
make SEED=0x2a    # un altro seme
make clean
```

Richiede un compilatore C per host con ASan/UBSan (gcc o clang). Il codice di
uscita è 0 in caso di PASS. Contesto: *Cosa garantisce il codificatore di trame*
nel capitolo «La catena di segnale DSP» della documentazione.
