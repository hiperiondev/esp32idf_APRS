/**
 * @file gps.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @note
 * This is based on other projects:
 *     VP-Digi: https://github.com/sq8vps/vp-digi
 *     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
 *     LibAPRS: https://github.com/markqvist/LibAPRS
 *
 *     please contact their authors for more information.
 *
 * @brief NMEA 0183 GNSS receiver subsystem (HW-248 / ATGM336H class module on
 *        a hardware UART).
 *
 * @details
 * Owns one UART port wired to a serial GNSS receiver, reassembles the NMEA
 * sentences the module streams at its fix rate, validates each one against its
 * own checksum and folds the recognised sentences into a single
 * ::gps_data_t snapshot that any other part of the firmware can read with
 * ::gps_snapshot.
 *
 * The subsystem is receive-only in the sense that matters to the operator: it
 * never writes configuration sentences to the module, so the module keeps
 * whatever output set and fix rate it powers up with. The UART's TX pin is
 * still claimed (and reserved in the web admin's GPIO registry) because the
 * pin is physically wired to the module's RX input on this board.
 *
 * Sentences understood: @c GGA (fix quality, satellites used, altitude, geoid
 * separation, HDOP), @c RMC (validity, date/time, position, ground speed,
 * course, magnetic variation), @c GSA (2D/3D fix mode and the PDOP/HDOP/VDOP
 * triplet), @c GSV (satellites in view) and @c VTG (ground speed and course).
 * The two-character talker prefix is ignored when matching, so a
 * multi-constellation module emitting @c $GNRMC, @c $GPRMC or @c $BDGSV is
 * handled the same way. Satellites in view are accumulated per talker and
 * summed, which is what a module reporting GPS, BeiDou and GLONASS in separate
 * @c GSV cycles requires to produce one meaningful total.
 *
 * Every field carries its own presence flag. A field is only ever marked
 * present once a sentence that actually carries it has been received with a
 * good checksum and a non-empty value, so a page rendering the snapshot can
 * distinguish "the module has not reported this" from "the module reported
 * zero".
 */

#ifndef GPS_H
#define GPS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @name GNSS serial port wiring (compile-time constants)
 *
 * The ONLY place the receiver's port and pins are defined. Applied by gps.c
 * when it brings the UART up and simultaneously removed from every GPIO
 * dropdown in the web admin via ::gps_gpio_is_reserved below, so a colliding
 * assignment is impossible to make from the UI.
 *
 * Defaults: UART2, GPIO16 = ESP32 RX (module TX), GPIO17 = ESP32 TX (module
 * RX), 9600 baud 8N1 - the power-up configuration of an HW-248 and of every
 * other ATGM336H-based board.
 *
 * @warning GPIO16 and GPIO17 are wired to the SPI PSRAM die inside an
 * ESP32-WROVER module and are unusable there. This board definition assumes a
 * WROOM-class module with no PSRAM; on a WROVER, move the receiver to another
 * free UART-capable pin pair here.
 * @{
 */
#ifndef GPS_UART_PORT
#define GPS_UART_PORT 2 /**< UART peripheral number the GNSS receiver is wired to. */
#endif

#ifndef GPS_UART_RX_GPIO
#define GPS_UART_RX_GPIO 16 /**< ESP32 receive pin; connects to the module's TX output. */
#endif

#ifndef GPS_UART_TX_GPIO
#define GPS_UART_TX_GPIO 17 /**< ESP32 transmit pin; connects to the module's RX input. */
#endif

#ifndef GPS_UART_BAUD
#define GPS_UART_BAUD 9600 /**< Line rate in bit/s, 8 data bits, no parity, 1 stop bit. */
#endif
/** @} */

/**
 * @brief Seconds without a valid sentence after which the snapshot is
 *        considered stale.
 *
 * A receiver that has lost power, been unplugged or been left with its TX line
 * open stops producing sentences entirely rather than reporting an invalid
 * fix, so age is the only thing that distinguishes "no fix" from "no module".
 * ::gps_data_t::link_up goes false past this many seconds and the position
 * fields stop being advertised as present.
 */
#define GPS_LINK_TIMEOUT_S 10

/**
 * @brief Maximum number of distinct NMEA talkers whose @c GSV satellite counts
 *        are tracked and summed into ::gps_data_t::sats_in_view.
 *
 * One slot per constellation the module reports separately (typically GP, BD,
 * GL and GA). A module using more talkers than this still reports a correct
 * total for the first ::GPS_MAX_TALKERS of them.
 */
#define GPS_MAX_TALKERS 4

/**
 * @brief Position-fix quality, as reported in the @c GGA fix-quality field.
 */
typedef enum {
    GPS_FIX_NONE = 0,      /**< No position fix available. */
    GPS_FIX_GPS = 1,       /**< Autonomous (standard positioning service) fix. */
    GPS_FIX_DGPS = 2,      /**< Differentially corrected fix. */
    GPS_FIX_PPS = 3,       /**< Precise positioning service fix. */
    GPS_FIX_RTK = 4,       /**< Real-time kinematic, integer ambiguities fixed. */
    GPS_FIX_RTK_FLOAT = 5, /**< Real-time kinematic, float solution. */
    GPS_FIX_ESTIMATED = 6, /**< Dead-reckoning estimate. */
    GPS_FIX_MANUAL = 7,    /**< Manual input mode. */
    GPS_FIX_SIMULATED = 8, /**< Simulation mode. */
} gps_fix_quality_t;

/**
 * @brief Solution dimensionality, as reported in the @c GSA fix-mode field.
 */
typedef enum {
    GPS_MODE_NOFIX = 1, /**< Tracking satellites, no solution yet. */
    GPS_MODE_2D = 2,    /**< Horizontal position only; altitude is not solved. */
    GPS_MODE_3D = 3,    /**< Horizontal position and altitude solved. */
} gps_fix_mode_t;

/**
 * @brief Everything the receiver has reported, as one self-consistent snapshot.
 *
 * Filled by gps.c as sentences arrive and copied out whole by ::gps_snapshot.
 * Each value has a matching @c has_* flag; read the value only when its flag is
 * true, since an unreported field keeps whatever the previous sentence cycle
 * left in it.
 */
typedef struct {
    bool link_up;              /**< A valid sentence arrived within ::GPS_LINK_TIMEOUT_S. */
    bool valid;                /**< @c RMC reported an active (non-warning) navigation solution. */
    gps_fix_quality_t quality; /**< Fix quality from @c GGA. */
    gps_fix_mode_t mode;       /**< Solution dimensionality from @c GSA. */

    bool has_position; /**< Latitude and longitude are solved and present. */
    double latitude;   /**< Latitude in signed decimal degrees, north positive. */
    double longitude;  /**< Longitude in signed decimal degrees, east positive. */

    bool has_altitude; /**< Altitude above mean sea level is present. */
    double altitude_m; /**< Altitude above mean sea level, metres. */
    bool has_geoid;    /**< Geoid separation is present. */
    double geoid_m;    /**< Height of the geoid above the WGS-84 ellipsoid, metres. */

    bool has_speed;    /**< Ground speed is present. */
    double speed_kmh;  /**< Speed over ground, km/h. */
    bool has_course;   /**< True course over ground is present. */
    double course_deg; /**< Course over ground, degrees true, 0..360. */
    bool has_magvar;   /**< Magnetic variation is present. */
    double magvar_deg; /**< Magnetic variation, degrees, east positive. */

    bool has_time;  /**< UTC time of the last fix is present. */
    uint8_t hour;   /**< UTC hour of fix, 0..23. */
    uint8_t minute; /**< UTC minute of fix, 0..59. */
    uint8_t second; /**< UTC second of fix, 0..59. */
    bool has_date;  /**< UTC date of the last fix is present. */
    uint16_t year;  /**< UTC year of fix, four digits. */
    uint8_t month;  /**< UTC month of fix, 1..12. */
    uint8_t day;    /**< UTC day of fix, 1..31. */

    bool has_sats_used;    /**< Satellite count used in the solution is present. */
    uint8_t sats_used;     /**< Satellites contributing to the current solution. */
    bool has_sats_in_view; /**< Satellite-in-view count is present. */
    uint8_t sats_in_view;  /**< Satellites in view, summed over every reporting talker. */

    bool has_hdop; /**< Horizontal dilution of precision is present. */
    double hdop;   /**< Horizontal dilution of precision. */
    bool has_pdop; /**< Position dilution of precision is present. */
    double pdop;   /**< Position dilution of precision. */
    bool has_vdop; /**< Vertical dilution of precision is present. */
    double vdop;   /**< Vertical dilution of precision. */

    uint32_t sentences_ok;  /**< Sentences received whose checksum verified. */
    uint32_t sentences_bad; /**< Sentences discarded on a checksum or framing error. */
    bool has_fix_age;       /**< A position fix has been obtained at least once since boot. */
    uint32_t fix_age_s;     /**< Seconds since the last sentence with a valid position. */
    bool has_link_age;      /**< A sentence has verified at least once since boot. */
    uint32_t link_age_s;    /**< Seconds since the last sentence of any kind verified. */
} gps_data_t;

/**
 * @brief Bring the GNSS subsystem up or down to match @c g_config.gps_en.
 *
 * Call once from application start-up, and again from the GPS page's save
 * handler whenever the operator moves the switch, so the change takes effect
 * without a reboot.
 *
 * With the switch off nothing is claimed at all: the UART driver is not
 * installed, the two pins are left alone and the reader task does not exist.
 * With it on, the UART is configured on the pins above and the reader task
 * starts. Failing to install the driver is logged and leaves the subsystem
 * inert - ::gps_snapshot then keeps reporting "no receiver" - rather than
 * aborting the boot, since a missing or miswired receiver must not cost the
 * station its igate, digipeater or beacons.
 *
 * Turning the switch off asks the reader task to finish its current pass and
 * exit before the UART driver is removed, so the port is never torn down from
 * under a task that is reading it.
 *
 * Safe to call from any task, and safe to call concurrently with itself: the
 * whole sequence (reading the current state, deciding, and installing or
 * removing the UART driver) runs under a module mutex of its own, so two saves
 * arriving together are applied one after the other instead of both acting on
 * the same "not running" reading. The second caller blocks until the first has
 * finished, which for a switch being turned off includes waiting for the reader
 * task to exit. ::gps_snapshot is not held up by any of this.
 */
void gps_apply_config(void);

/**
 * @brief True while the receiver is actually running.
 *
 * This is the enable switch as the firmware sees it, not merely as it was
 * configured: it is false when the operator has the receiver switched off AND
 * when the switch is on but the UART could not be brought up. Consumers that
 * want to know whether GNSS data is available at all, without taking a
 * snapshot, test this; consumers that want the data itself just call
 * ::gps_snapshot, whose false return says the same thing.
 *
 * @return true if the UART is installed and the reader task is running.
 */
bool gps_enabled(void);

/**
 * @brief Copy the current receiver state into @p out under the module lock.
 *
 * The copy is atomic with respect to the reader task, so every field of @p out
 * comes from the same instant and a caller never sees a latitude from one fix
 * paired with a longitude from the next.
 *
 * @param out Destination snapshot; untouched when @c NULL is passed.
 * @return true if the receiver is running and @p out was filled, false if it
 *         is switched off in the configuration or its UART bring-up failed (in
 *         which case @p out is zeroed, which reads as "no link, no fix").
 */
bool gps_snapshot(gps_data_t *out);

/**
 * @brief True if @p gpio is one of the pins the GNSS receiver's UART
 *        permanently occupies (RX or TX).
 *
 * Every GPIO @c \<select\> the web admin renders (message-alarm pin, ...)
 * consults this - directly, or through the GPIO registry in
 * web_gpio_collect_used() - and skips any pin for which it returns true, so
 * those pins can never be handed to another peripheral from the web UI. Kept
 * as a header-only inline, and this header kept free of any ESP-IDF include,
 * so consumers that only need the reserved-pin rule can apply it without
 * pulling in the UART machinery.
 *
 * @param gpio GPIO number to test (any int; values that match neither pin
 *             return false).
 * @return true if the pin belongs to the GNSS receiver's UART, false otherwise.
 */
static inline bool gps_gpio_is_reserved(int gpio) {
    return (gpio == GPS_UART_RX_GPIO) || (gpio == GPS_UART_TX_GPIO);
}

#endif // GPS_H
