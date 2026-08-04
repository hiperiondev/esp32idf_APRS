.. _es-sensor-framework:

====================
El marco de sensores
====================

``sensors_local`` (``components/sensors_local/``) es el marco en tiempo de
ejecución que permite a sensores de hardware reales (o simulados) alimentar los
subsistemas de Informe Meteorológico y Telemetría de la propia estación, **sin
que el núcleo necesite jamás una lista codificada** de "los sensores que soporta
esta compilación". Si quieres conectar un BME280, un DS18B20, un ADS1115, una
sonda de humedad de suelo, un divisor de tensión de batería o cualquier otra
cosa, este es el mecanismo a usar.

Por qué un marco de controladores
=================================

Firmwares APRS anteriores de este linaje usaban un array de tamaño fijo de
"ranuras de sensor" en ``g_config``, cada una descrita por un ``type``/``port``/
``address`` numérico que alguna sentencia ``switch`` central interpretaba. Cada
sensor nuevo significaba editar ese switch, recompilar, y esperar que los IDs
numéricos no colisionaran.

``sensors_local`` invierte esto:

* El núcleo (``sensors_local.c``) **no sabe nada** de ningún sensor específico.
  Solo mantiene una lista de estructuras "controlador" opacas y llama a un puñado
  de punteros a función sobre ellas.
* Cada sensor real vive en su **propio archivo ``.c``** bajo ``drivers/`` y se
  añade a sí mismo a la lista **automáticamente al arrancar**, antes incluso de
  que ``app_main()`` se ejecute, mediante un constructor C oculto tras la macro
  ``SENSORS_LOCAL_DRIVER_AUTOREGISTER``.

El resultado práctico: añadir un sensor es "soltar un archivo nuevo en
``drivers/``, listarlo en el ``CMakeLists.txt`` del componente, recompilar" —
nada en ``sensors_local.c``, ``weather.c``, ``sensors_local.h`` ni ninguna
cabecera necesita cambiar.

Las dos familias de carga útil
==============================

Un controlador rellena campos de nivel de aplicación ya agrupados por tipo de
carga útil APRS, definidos en el componente separado ``weather_telemetry``:

.. list-table::
   :header-rows: 1
   :widths: 20 26 54

   * - Familia
     - Bit
     - Estructura destino / consumidor
   * - **Meteo**
     - ``SENSOR_LOCAL_DATA_WEATHER`` (``1u<<0``)
     - ``aprs_weather_report_t`` → ``weather.c`` → baliza WX al aire
   * - **Telemetría**
     - ``SENSOR_LOCAL_DATA_TELEMETRY`` (``1u<<1``)
     - ``aprs_telemetry_report_t`` (A1–A5 + B1–B8) → ``telemetry.c`` → baliza
       ``T#nnn``
   * - *(reservado)*
     - p. ej. ``SENSOR_LOCAL_DATA_GPS = 1u<<2``
     - una estructura futura — véase abajo

Un solo controlador puede anunciar **uno u otro o ambos** bits en sus
``capabilities``. ``SENSOR_LOCAL_DATA_ALL`` es el OR de cada bit actualmente
definido; está disponible para un consumidor que realmente quiera todas las
familias a la vez. ``weather.c`` **no** lo usa en su pasada a 1 Hz: refresca la
telemetría con una llamada agregada ``SENSOR_LOCAL_DATA_TELEMETRY`` y luego lee
cada campo meteorológico por separado con
``sensors_local_save_one(..., SENSOR_LOCAL_DATA_WEATHER)``, para que cada campo
WX respete el controlador elegido para él en la página Weather.

Anatomía de un controlador
==========================

Cada controlador es una instancia de ``sensor_local_driver_t``:

.. code-block:: c

   struct sensor_local_driver {
       const char *name;      // id estable, único, legible por humanos
       uint32_t capabilities; // OR de WEATHER / TELEMETRY (debe ser no-cero)

       sensor_local_init_fn_t init; // puesta en marcha única opcional (puede ser NULL)
       sensor_local_save_fn_t save; // OBLIGATORIO: lee el sensor

       const sensor_local_properties_t *properties; // qué campos WX / canales TLM

       void *ctx; // estado privado del controlador, opaco al registro

       bool initialized; // propiedad del registro
       bool failed;       // propiedad del registro
   };

Los dos roles de puntero a función:

* **``init(self)``** — llamado como mucho una vez, perezosamente, la primera vez
  que se necesita el controlador (o ansiosamente al arrancar). Abre el bus, sondea
  el chip, asigna estado privado. Devuelve ``ESP_OK`` en éxito; cualquier otro
  valor **marca el controlador como fallido permanentemente** y se salta de ahí
  en adelante.
* **``save(self, data, kind)``** — EL punto de entrada común, llamado en cada
  ciclo de refresco (1 Hz). ``kind`` ya está enmascarado solo a los bits que
  tanto el llamador quiere *como* el controlador anunció. El controlador lee su
  sensor y escribe directamente en el contenedor ``data`` propiedad del llamador,
  activando la bandera ``enabled[…]`` de cada campo. Debe tolerar un destino
  vacío (``data->weather_qty == 0``) no haciendo nada para esa familia.

Deliberadamente no hay callback de desmontaje: el firmware nunca quita un
controlador, así que una ranura de limpieza solo prometería trabajo que jamás
podría ejecutarse.

El registro
===========

``sensors_local.c`` implementa el registro como un pequeño array protegido por
mutex, ampliable en heap, de **punteros** a controlador (nunca copias — el
almacenamiento de tu estructura ``static`` es lo que vive en la tabla):

.. code-block:: text

   sensors_local_init()          // crea el mutex del registro
   sensors_local_register(drv)   // añade; rechaza save NULL, nombre vacío,
                                 //   nombre duplicado, o capabilities == NONE
   sensors_local_count()         // cuántos controladores registrados
   sensors_local_get(index)      // obtener por posición (desplegable página Weather)
   sensors_local_init_all()      // init() ansiosamente cada controlador
   sensors_local_save(data,kind) // recorre la tabla; init() perezoso, luego save()
   sensors_local_save_one(i,...) // lee UN controlador por índice (vista previa en vivo)

``sensors_local_register()`` puede ejecutarse **antes de que exista el
planificador de FreeRTOS**, porque ``SENSORS_LOCAL_DRIVER_AUTOREGISTER`` dispara
desde un ``__attribute__((constructor))``. En ese punto el mutex del registro aún
no existe — los ayudantes de lock/unlock son no-ops mientras es NULL, lo que es
seguro solo porque toda esa fase es de un solo hilo. La primera llamada real a
``sensors_local_init()`` (desde ``weather_start()``) crea el mutex y hace
seguro-de-hilos cada acceso posterior.

Que un controlador falle su ``init()`` o devuelva un error de ``save()`` se
registra y se **salta**; nunca aborta la pasada para los demás controladores.

Flujo de datos de extremo a extremo
===================================

.. code-block:: text

   arranque (antes de app_main)
     └─ constructor de cada drivers/*.c → SENSORS_LOCAL_DRIVER_AUTOREGISTER
          → sensors_local_register(&my_driver)

   weather_start()  (una vez, al arrancar)
     ├─ sensors_local_init()          ← crea el mutex del registro
     ├─ sensors_local_init_all()      ← ejecuta init() en cada controlador
     └─ registra weather_service_1hz() y weather_beacon_service()

   weather_service_1hz()   (1 Hz)
     ├─ limpia las banderas "enabled" del contenedor
     ├─ sensors_local_save(&data, SENSOR_LOCAL_DATA_TELEMETRY)
     │    └─ cada controlador con capacidad TELEMETRY: init() perezoso, luego save()
     ├─ por cada campo WX f con wx_sensor_enable[f] y canal asignado:
     │    └─ sensors_local_save_one(wx_sensor_ch[f], &scratch, ..._WEATHER)
     │         └─ copia solo ese campo al reporte vivo
     └─ acumula cualquier campo "Averaged" en una suma/cuenta corriente

   weather_beacon_service()   (cada wx_interval s, si wx_en)
     ├─ resuelve campos (en vivo o promediado, según casilla)
     ├─ construye la línea TNC2 "!lat/lon_WIND…"
     └─ transmite por RF y/o APRS-IS

El punto clave para cualquiera que añada un sensor: **nunca llamas tú mismo a
nada de ``weather.c`` ni de la administración web.** Registrar el controlador es
toda la integración; el refresco a 1 Hz, el promediado, la codificación WX al
aire y el selector de canal lo descubren todo a través del registro.

Añadir un sensor, paso a paso
=============================

#. **Decide la familia.** Un BME280/DS18B20 es Meteo; un divisor de batería,
   interruptor reed o sonda de suelo es Telemetría (analógica o digital); una
   placa combo puede ser ambas.
#. **Copia un esqueleto.** Copia el controlador de ejemplo correspondiente
   (``drivers/example/sensor_local_weather_example.c`` o
   ``…_telemetry_example.c``) a una carpeta nueva, p. ej. ``drivers/bme280/``,
   con su propio ``bme280_properties.h``.
#. **Rellena ``init()``** — configura/sondea el bus, lee el chip-ID, asigna
   almacenamiento de calibración en ``self->ctx``, devuelve ``ESP_OK`` solo cuando
   estés seguro.
#. **Rellena ``save()``** — lee el sensor, convierte a las unidades de ingeniería
   que documenta ``weather_telemetry.h`` (°F, mph, décimas de mb, centésimas de
   pulgada…), escribe el/los valor(es) y activa la(s) bandera(s) ``enabled[…]``
   correspondiente(s). Comprueba siempre ``kind`` y los punteros de destino
   primero.
#. **Declara el descriptor** y ``SENSORS_LOCAL_DRIVER_AUTOREGISTER(...)`` sobre
   él. ``name`` debe ser único — es lo que aparece en el desplegable de la página
   Weather.
#. **Lista el fuente** en ``components/sensors_local/CMakeLists.txt`` y
   ``idf.py build``. El componente enlaza con ``WHOLE_ARCHIVE`` para que el
   ``--gc-sections`` del enlazador no pueda descartar un objeto cuya única
   referencia es su propio constructor.
#. **Mapéalo en la página Weather (o Telemetry)** — el nombre de tu controlador
   ahora aparece en el desplegable de canal de cada campo relevante
   automáticamente.

Múltiples instancias, manejo de errores, seguridad de hilos
===========================================================

* **Múltiples instancias** del mismo tipo de sensor coexisten: da a cada una un
  ``name`` distinto (``bme280-indoor`` / ``bme280-outdoor``), su propio ``ctx``, y
  su propia dirección/bus/GPIO horneada en ese ``ctx``.
* Un error de ``init()`` marca el controlador como fallido **permanentemente**:
  el registro no tiene punto de entrada para desregistrar, así que la marca dura
  hasta el próximo reinicio. Un error de ``save()`` se registra y se salta
  **solo para ese ciclo** — el siguiente tick lo reintenta, así que un hipo
  ocasional del bus no deshabilita el controlador.
* Las llamadas al registro están todas protegidas por mutex. El
  ``init()``/``save()`` propio de un controlador **no** los envuelve
  el marco en ningún cerrojo — si el ``ctx`` de un controlador se toca desde algo
  más que el refresco a 1 Hz (p. ej. una ISR), el controlador es responsable de su
  propia sincronización.

El controlador BMP180 integrado
===============================

``drivers/bmp180/bmp180.c`` es un controlador I2C de temperatura/presión real
construido sobre ``esp-idf-lib/bmp180``. Sus pines I2C son configurables por
``#define`` en ``BMP180.h`` (por defecto GPIO21 = SDA, GPIO22 = SCL); esos pines
se excluyen de cada selector GPIO de la administración web para que no puedan
doble-asignarse. Anuncia Meteo y escribe temperatura y presión barométrica. Está
condicionado tras ``CONFIG_SENSORS_LOCAL_BMP180_DRIVER``.

Añadir una *clase* de sensor completamente nueva
================================================

Meteo y Telemetría no son las únicas familias que el marco puede llevar. Para
añadir, digamos, GPS:

#. Añade ``SENSOR_LOCAL_DATA_GPS = 1u << 2`` a ``sensor_local_data_kind_t`` y
   agrégalo con OR a ``SENSOR_LOCAL_DATA_ALL``.
#. Añade la estructura destino en la que aterriza un fix de GPS (a
   ``weather_telemetry.h``).
#. Escribe controlador(es) cuyas ``capabilities`` incluyan el bit nuevo.
#. Filtra el registro con ``driver->capabilities & SENSOR_LOCAL_DATA_GPS`` donde
   sea que un consumidor necesite la clase nueva. El registro,
   ``sensors_local_save()`` y cada controlador existente quedan completamente
   inafectados.
