.. _es-localization:

============
Localización
============

La administración web está completamente localizada a **inglés, español e
italiano**. El modelo es **un idioma por imagen de firmware** — no hay cambio de
idioma en ejecución, y las cadenas de ningún otro idioma se compilan en una
compilación dada.

.. note::

   Esto aplica a la administración web del *firmware*. *Esta documentación* está
   disponible por separado en los tres idiomas a la vez, con un selector de
   idioma por página — eso es una propiedad de la documentación, no del firmware.

Cómo funciona
=============

* ``app_config.h`` define los códigos de idioma ``LANG_EN 0``, ``LANG_ES 1``,
  ``LANG_IT 2`` y el ``LANGUAGE`` activo (por defecto ``LANG_EN``).
* ``translations/translations.h`` es el **único** lugar que decide qué
  ``lang_xx.h`` se incluye, vía ramas ``#if LANGUAGE == …``.
* Cada cadena visible al usuario en la administración web pasa por una macro
  ``TR_xxx``. Hay 1051 de esas macros, y cada uno de los tres archivos
  ``lang_*.h`` define todas. 226 de ellas son las cadenas de ayuda contextual
  ``TR_H_xxx`` que hay tras el signo de interrogación que cierra cada etiqueta
  de opción, de modo que los globos de ayuda se traducen igual que las
  etiquetas que explican.

Seleccionar el idioma en compilación
====================================

.. code-block:: bash

   idf.py build                      # inglés (por defecto)
   idf.py build -DLANGUAGE=LANG_ES   # español
   idf.py build -DLANGUAGE=LANG_IT   # italiano

También puedes fijarlo en ``CMakeLists.txt`` (``set(LANGUAGE LANG_ES)`` /
``target_compile_definitions``) o cambiar el valor por defecto en
``app_config.h``.

Añadir un idioma
================

#. Copia ``translations/lang_en.h`` → ``lang_xx.h``, traduce cada literal, y
   mantén cada nombre de macro idéntico.
#. ``#define LANG_XX <siguiente número libre>`` en ``app_config.h``.
#. Añade una rama ``#elif LANGUAGE == LANG_XX`` en ``translations.h``.
#. Compila con ``-DLANGUAGE=LANG_XX``.

.. important::

   Que falte un ``TR_xxx`` en un idioma es un **error de compilación en la
   compilación de ese idioma** — intencionado, para que las cadenas sin traducir
   nunca puedan salir en silencio. Por eso los tres archivos ``lang_*.h`` siempre
   llevan exactamente el mismo conjunto de macros.
