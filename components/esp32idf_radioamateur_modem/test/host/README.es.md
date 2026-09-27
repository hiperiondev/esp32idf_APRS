# Test de regresión en host — transmisor HDLC

🇬🇧 [English](README.md) · 🇪🇸 Español · 🇮🇹 [Italiano](README.it.md)

`test_ax25_tx_hdlc.c` compila los fuentes reales `src/ax25.c`, `src/crc_ccit.c`
y los de FX.25 (`src/fx25.c`, `lwfec/rs.c`, `lwfec/gf.c`) para el host, contra
las cabeceras ESP-IDF sustitutas de `stubs/`, con AddressSanitizer y
UndefinedBehaviorSanitizer.

Envía tramas UI AX.25 aleatorias por `Ax25WriteTxFrame()` →
`Ax25TransmitCheck()` → `Ax25GetTxBit()` y verifica cada transmisión:

- bit a bit contra un codificador HDLC de referencia (trama + FCS con relleno
  como un único campo, incluido el 0 de relleno que corresponde cuando una racha
  de cinco 1 termina en el último bit de la FCS);
- con el receptor propio (`Ax25BitParse()` / `Ax25ReadNextRxFrame()`);
- con un receptor estricto que sigue las reglas de `hdlc_rec.c` de Direwolf y
  exige exactamente siete bits de datos en el flag de cierre.

Una segunda pasada envía tramas como bloques FX.25 por el receptor propio.
Mientras se reloja `Ax25GetTxBit()`, las páginas que contienen `Fx25ModeList` se
desmapean con `mprotect()`, en lugar de la caché de la flash deshabilitada bajo
la ISR del DAC segura frente a la caché: una lectura de la tabla desde la ruta
de bits de transmisión termina la corrida con un fallo de segmentación.

Una tercera pasada arma tramas con 0 a 8 digipetidores desde texto TNC2 con
`ax25_encode()` y `hdlcFrame()` y verifica que solo la última dirección lleve el
bit de fin de dirección, que `ax25_decode()` recupere todos los digipetidores y
que la trama sobreviva a una transmisión y al receptor propio.

Una cuarta pasada corre en semidúplex y mide cuánto espera cada trama encolada
antes de transmitir: con `Ax25TimeSlot(0)` cada trama sale de inmediato, incluso
si antes se fijó una ranura distinta de cero; con una ranura de 2000 ms cada
trama espera al menos 2000 ms.

Una quinta pasada verifica el final de una activación y la compuerta de
activación: para la cola por defecto y varias longitudes de `Ax25TxTail()` cada
trama debe ir seguida exactamente de una bandera de cierre más la cola
redondeada hacia arriba a banderas enteras, y una trama encolada no debe
activarse mientras `getTransmit()` sea verdadero o siga pendiente el desmontaje
de la desactivación anterior; luego debe activarse en cuanto ambos se liberan.

El informe indica cuántas tramas terminaron su FCS en cinco 1, así que un PASS
siempre cubre ese caso. La corrida necesita un host POSIX (`mprotect()`,
`sysconf()`).

```bash
make              # compilar y ejecutar, semilla por defecto
make SEED=0x2a    # otra semilla
make clean
```

Requiere un compilador C de host con ASan/UBSan (gcc o clang). El código de
salida es 0 en PASS. Contexto: *Qué garantiza el codificador de tramas* en el
capítulo «La cadena de señal DSP» de la documentación.
