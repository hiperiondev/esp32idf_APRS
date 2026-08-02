.. _it-localization:

==============
Localizzazione
==============

L'amministrazione web è completamente localizzata in **inglese, spagnolo e
italiano**. Il modello è **una lingua per immagine firmware** — non c'è cambio di
lingua a runtime, e le stringhe di nessun'altra lingua sono compilate in una data
build.

.. note::

   Questo si applica all'amministrazione web del *firmware*. *Questa
   documentazione* è disponibile separatamente in tutte e tre le lingue
   contemporaneamente, con un selettore di lingua per pagina — questa è una
   proprietà della documentazione, non del firmware.

Come funziona
=============

* ``app_config.h`` definisce i codici di lingua ``LANG_EN 0``, ``LANG_ES 1``,
  ``LANG_IT 2`` e la ``LANGUAGE`` attiva (predefinita ``LANG_EN``).
* ``translations/translations.h`` è l'**unico** posto che decide quale
  ``lang_xx.h`` è incluso, tramite rami ``#if LANGUAGE == …``.
* Ogni stringa visibile all'utente nell'amministrazione web passa per una macro
  ``TR_xxx``. Ci sono 524 di queste macro, e ciascuno dei tre file ``lang_*.h``
  le definisce tutte.

Selezionare la lingua in compilazione
=====================================

.. code-block:: bash

   idf.py build                      # inglese (predefinito)
   idf.py build -DLANGUAGE=LANG_ES   # spagnolo
   idf.py build -DLANGUAGE=LANG_IT   # italiano

Puoi anche fissarla in ``CMakeLists.txt`` (``set(LANGUAGE LANG_ES)`` /
``target_compile_definitions``) o cambiare il valore predefinito in
``app_config.h``.

Aggiungere una lingua
=====================

#. Copia ``translations/lang_en.h`` → ``lang_xx.h``, traduci ogni letterale, e
   mantieni ogni nome di macro identico.
#. ``#define LANG_XX <prossimo numero libero>`` in ``app_config.h``.
#. Aggiungi un ramo ``#elif LANGUAGE == LANG_XX`` in ``translations.h``.
#. Compila con ``-DLANGUAGE=LANG_XX``.

.. important::

   La mancanza di un ``TR_xxx`` in una lingua è un **errore di compilazione nella
   build di quella lingua** — intenzionale, così che le stringhe non tradotte non
   possano mai uscire in silenzio. Ecco perché i tre file ``lang_*.h`` portano
   sempre esattamente lo stesso insieme di macro.
