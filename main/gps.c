// @file gps.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief NMEA 0183 GNSS receiver subsystem: owns the receiver's UART,
// reassembles and checksums the sentence stream, and folds the recognised
// sentences into the one ::gps_data_t snapshot ::gps_snapshot hands out.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h" // g_config.gps_en: the operator's enable switch
#include "gps.h"
#include "sched_time.h" // sched_mono_seconds(): monotonic clock for the staleness ages

static const char *TAG = "gps";

// A conforming NMEA 0183 sentence is at most 82 characters including the
// leading '$' and the trailing CR/LF. The assembly buffer is sized past that
// so a receiver that emits a longer proprietary sentence is discarded on its
// own terms (overlong -> dropped) instead of being silently cut into two
// fragments, the second of which could still checksum as something else.
#define GPS_SENTENCE_MAX 128

// Field pointers one sentence can be split into. The longest sentence this
// parser reads is GSA, whose 17 fields plus the sentence identifier and the
// checksum field fit well inside this; anything with more fields is parsed up
// to the limit and the rest ignored, which never affects the fields read here
// because they all sit near the front.
#define GPS_MAX_FIELDS 24

// UART receive ring maintained by the driver. At 9600 baud a full one-second
// sentence burst from a multi-constellation module (GGA + RMC + VTG + GLL +
// several GSA and GSV lines) is on the order of 700 bytes, so this holds more
// than one whole cycle: the reader task can be preempted for a long moment
// without the driver dropping the tail of a fix.
#define GPS_UART_RX_BUF 2048

// Bytes lifted out of the driver per read call, and how long a read waits
// before returning with whatever has arrived. The timeout is what makes the
// task loop at a bounded rate when the receiver is absent or silent, so the
// staleness bookkeeping below keeps running with no bytes coming in.
#define GPS_READ_CHUNK   256
#define GPS_READ_WAIT_MS 200

// The reader task only tokenises short ASCII lines and does a handful of
// strtod() conversions, but strtod() on this toolchain pulls in the full
// locale-aware conversion path, so the budget is set with headroom rather
// than trimmed to a measured minimum. Use uxTaskGetStackHighWaterMark() on
// this task before changing it.
#define GPS_TASK_STACK_BYTES 4096

// Below the network and modem service tasks: a late fix costs nothing, while
// a late AX.25 frame is lost airtime.
#define GPS_TASK_PRIORITY 4

// How long a shutdown waits for the reader task to leave its loop, and how
// often it re-checks. The task can be mid-read when the switch is thrown, so
// the wait has to cover one full GPS_READ_WAIT_MS pass with room to spare; the
// UART driver is only removed once the task has confirmed it is gone, because
// deleting the driver under a task still reading it is what turns a
// configuration change into a crash.
#define GPS_STOP_TIMEOUT_MS 2000
#define GPS_STOP_POLL_MS    10

// One talker's most recent "satellites in view" report. A multi-constellation
// receiver runs an independent GSV cycle per constellation, all carrying the
// same field in the same place, so the only way to a meaningful total is to
// remember each talker's last figure and sum them.
typedef struct {
    char talker[3]; // two-character talker prefix, NUL terminated; empty = free slot
    uint8_t in_view;
} gps_talker_t;

static SemaphoreHandle_t s_lock;

// Shutdown handshake between whoever throws the switch and the reader task.
// s_stop_request is raised by the requester and observed by the task at the
// top of each pass; once raised it is never retracted from outside the task,
// so a requester that gives up on waiting cannot un-notice a request the task
// already saw. s_task_alive is lowered by the task as the last thing it does,
// and is what tells the requester the port is free. Both are plain volatile
// bools written by exactly one side each, which is all the synchronisation a
// single word needs on this MCU.
//
// There is no separate "running" flag: whether the receiver is up is read
// straight off s_task_alive plus whether the UART port is currently
// installed, so a task that dies (on request or otherwise) is immediately
// visible to gps_apply_config() on its next call instead of leaving a stale
// "enabled" state that only a reboot clears.
static volatile bool s_stop_request;
static volatile bool s_task_alive;
static bool s_port_installed;

// The live receiver state. Everything outside this file reads it through
// gps_snapshot(), which copies it whole under s_lock.
static gps_data_t s_data;
static gps_talker_t s_talkers[GPS_MAX_TALKERS];

// Monotonic timestamps behind the two ages reported in the snapshot, each
// with its own "has this ever happened" flag. Held separately from gps_data_t
// because the ages are derived at read time: a snapshot taken a minute after
// the last sentence has to report sixty seconds even though nothing has
// updated the state in between. The flags exist because zero is a legitimate
// timestamp - the monotonic clock reads 0 for the first second after boot, so
// a sentence arriving in that second would otherwise be indistinguishable
// from no sentence at all.
static int64_t s_last_sentence_s;
static bool s_have_sentence;
static int64_t s_last_fix_s;
static bool s_have_fix;

static void gps_lock(void) {
    if (s_lock)
        xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void gps_unlock(void) {
    if (s_lock)
        xSemaphoreGive(s_lock);
}

// -------------------------------------------------------------------------
// Sentence-level helpers
// -------------------------------------------------------------------------

// Value of one hexadecimal digit, or -1 if c is not one.
static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

// Verifies the "*hh" checksum of a complete sentence and cuts it off.
//
// line points at the sentence body (the leading '$' already removed) and is
// modified in place: on success the '*' is replaced by a NUL, so the caller is
// left with exactly the character range the checksum covers. NMEA defines that
// range as everything between '$' and '*' exclusive, XOR-folded a byte at a
// time.
static bool checksum_ok(char *line) {
    char *star = strchr(line, '*');
    if (star == NULL || star[1] == 0 || star[2] == 0)
        return false;

    int hi = hex_digit(star[1]);
    int lo = hex_digit(star[2]);
    if (hi < 0 || lo < 0)
        return false;

    uint8_t want = (uint8_t)((hi << 4) | lo);
    uint8_t got = 0;
    for (char *p = line; p < star; p++)
        got ^= (uint8_t)*p;

    if (got != want)
        return false;

    *star = 0;
    return true;
}

// Splits a checksum-stripped sentence into comma-separated fields in place.
//
// Empty fields are preserved as empty strings, which matters throughout NMEA:
// a receiver with no fix sends the sentence with its position fields blank
// rather than omitting them, and that blank is exactly what tells the parser
// the value is absent. Returns the number of fields stored, capped at
// GPS_MAX_FIELDS.
static int split_fields(char *line, char **fields, int max_fields) {
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p != 0 && n < max_fields; p++) {
        if (*p == ',') {
            *p = 0;
            fields[n++] = p + 1;
        }
    }
    return n;
}

// True when field index idx exists and carries a value.
static bool has_field(char **fields, int count, int idx) {
    return idx < count && fields[idx][0] != 0;
}

// Numeric value of field idx, or 0.0 when the field is absent or empty. Only
// ever called behind has_field(), so the fallback never reaches the snapshot.
static double field_num(char **fields, int count, int idx) {
    if (!has_field(fields, count, idx))
        return 0.0;
    return strtod(fields[idx], NULL);
}

// Converts an NMEA "ddmm.mmmm" / "dddmm.mmmm" angle plus its hemisphere letter
// into signed decimal degrees, north and east positive.
//
// The degrees field is fixed width (two characters for latitude, three for
// longitude) and the minutes field runs to the decimal point, so the split
// point is found from the position of the '.' rather than assumed: that keeps
// the conversion correct for a receiver emitting a different number of decimal
// places, which modules do vary in.
static bool parse_angle(const char *value, const char *hemi, double *out) {
    if (value == NULL || value[0] == 0 || hemi == NULL || hemi[0] == 0)
        return false;

    const char *dot = strchr(value, '.');
    size_t int_len = (dot != NULL) ? (size_t)(dot - value) : strlen(value);
    if (int_len < 3)
        return false; // fewer digits than "dmm" cannot carry degrees and minutes

    size_t deg_len = int_len - 2;
    char deg_txt[5];
    if (deg_len >= sizeof(deg_txt))
        return false;
    memcpy(deg_txt, value, deg_len);
    deg_txt[deg_len] = 0;

    double degrees = strtod(deg_txt, NULL);
    double minutes = strtod(value + deg_len, NULL);
    double result = degrees + minutes / 60.0;

    if (hemi[0] == 'S' || hemi[0] == 's' || hemi[0] == 'W' || hemi[0] == 'w')
        result = -result;

    *out = result;
    return true;
}

// Parses an NMEA "hhmmss.sss" time of day into the snapshot's time fields.
static bool parse_time(const char *value, gps_data_t *d) {
    if (value == NULL || strlen(value) < 6)
        return false;
    for (int i = 0; i < 6; i++) {
        if (value[i] < '0' || value[i] > '9')
            return false;
    }
    d->hour = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
    d->minute = (uint8_t)((value[2] - '0') * 10 + (value[3] - '0'));
    d->second = (uint8_t)((value[4] - '0') * 10 + (value[5] - '0'));
    return d->hour < 24 && d->minute < 60 && d->second < 61; // 60 is a legal leap second
}

// Parses an NMEA "ddmmyy" date into the snapshot's date fields.
//
// The two-digit year is expanded into the 2000s. NMEA carries no century, and
// the alternative - a pivot year - would only move the wrap from 2100 to some
// nearer date while making the reported year depend on a constant nobody
// maintains.
static bool parse_date(const char *value, gps_data_t *d) {
    if (value == NULL || strlen(value) < 6)
        return false;
    for (int i = 0; i < 6; i++) {
        if (value[i] < '0' || value[i] > '9')
            return false;
    }
    uint8_t day = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
    uint8_t month = (uint8_t)((value[2] - '0') * 10 + (value[3] - '0'));
    uint16_t year = (uint16_t)(2000 + (value[4] - '0') * 10 + (value[5] - '0'));
    if (day < 1 || day > 31 || month < 1 || month > 12)
        return false;
    d->day = day;
    d->month = month;
    d->year = year;
    return true;
}

// Records one talker's satellites-in-view count and recomputes the total.
//
// Slots are claimed on first sight and never released: the set of
// constellations a given receiver reports is fixed by its firmware, so the
// table converges after the first full GSV cycle and the sum stays stable
// afterwards. A receiver using more talkers than the table holds simply stops
// contributing past the last slot rather than cycling entries in and out,
// which would make the total oscillate.
static void note_sats_in_view(const char *talker, uint8_t in_view) {
    int slot = -1;
    for (int i = 0; i < GPS_MAX_TALKERS; i++) {
        if (s_talkers[i].talker[0] == 0) {
            if (slot < 0)
                slot = i;
            continue;
        }
        if (strcmp(s_talkers[i].talker, talker) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    strncpy(s_talkers[slot].talker, talker, sizeof(s_talkers[slot].talker) - 1);
    s_talkers[slot].talker[sizeof(s_talkers[slot].talker) - 1] = 0;
    s_talkers[slot].in_view = in_view;

    unsigned total = 0;
    for (int i = 0; i < GPS_MAX_TALKERS; i++) {
        if (s_talkers[i].talker[0] != 0)
            total += s_talkers[i].in_view;
    }
    s_data.sats_in_view = (uint8_t)((total > 255u) ? 255u : total);
    s_data.has_sats_in_view = true;
}

// -------------------------------------------------------------------------
// Per-sentence parsers. Each is called with the sentence already split into
// fields, field 0 being the talker+type identifier, and updates only the parts
// of the snapshot its sentence actually carries.
// -------------------------------------------------------------------------

// GGA: time, position, fix quality, satellites used, HDOP, altitude and geoid
// separation.
static void parse_gga(char **f, int n) {
    if (has_field(f, n, 1) && parse_time(f[1], &s_data))
        s_data.has_time = true;

    if (has_field(f, n, 6)) {
        int q = atoi(f[6]);
        s_data.quality = (q >= GPS_FIX_NONE && q <= GPS_FIX_SIMULATED) ? (gps_fix_quality_t)q : GPS_FIX_NONE;
    }

    // A quality field of 0 means the receiver is reporting no fix, and in that
    // state it fills the position fields with the last known values or with
    // blanks depending on the module. Gating the position on the quality field
    // is what keeps a stale coordinate from being presented as current.
    if (s_data.quality != GPS_FIX_NONE) {
        double lat, lon;
        if (has_field(f, n, 2) && has_field(f, n, 3) && has_field(f, n, 4) && has_field(f, n, 5) && parse_angle(f[2], f[3], &lat) &&
            parse_angle(f[4], f[5], &lon)) {
            s_data.latitude = lat;
            s_data.longitude = lon;
            s_data.has_position = true;
            s_last_fix_s = sched_mono_seconds();
            s_have_fix = true;
        }
        if (has_field(f, n, 9)) {
            s_data.altitude_m = field_num(f, n, 9);
            s_data.has_altitude = true;
        }
        if (has_field(f, n, 11)) {
            s_data.geoid_m = field_num(f, n, 11);
            s_data.has_geoid = true;
        }
    }

    if (has_field(f, n, 7)) {
        int used = atoi(f[7]);
        s_data.sats_used = (uint8_t)((used < 0) ? 0 : (used > 255) ? 255 : used);
        s_data.has_sats_used = true;
    }
    if (has_field(f, n, 8)) {
        s_data.hdop = field_num(f, n, 8);
        s_data.has_hdop = true;
    }
}

// RMC: navigation validity, date and time, position, ground speed, course and
// magnetic variation.
static void parse_rmc(char **f, int n) {
    if (has_field(f, n, 1) && parse_time(f[1], &s_data))
        s_data.has_time = true;

    s_data.valid = has_field(f, n, 2) && (f[2][0] == 'A' || f[2][0] == 'a');

    if (s_data.valid) {
        double lat, lon;
        if (has_field(f, n, 3) && has_field(f, n, 4) && has_field(f, n, 5) && has_field(f, n, 6) && parse_angle(f[3], f[4], &lat) &&
            parse_angle(f[5], f[6], &lon)) {
            s_data.latitude = lat;
            s_data.longitude = lon;
            s_data.has_position = true;
            s_last_fix_s = sched_mono_seconds();
            s_have_fix = true;
        }
        // Knots on the wire; the snapshot carries km/h so every consumer sees
        // one unit and the conversion exists in one place.
        if (has_field(f, n, 7)) {
            s_data.speed_kmh = field_num(f, n, 7) * 1.852;
            s_data.has_speed = true;
        }
        if (has_field(f, n, 8)) {
            s_data.course_deg = field_num(f, n, 8);
            s_data.has_course = true;
        }
    }

    if (has_field(f, n, 9) && parse_date(f[9], &s_data))
        s_data.has_date = true;

    if (has_field(f, n, 10)) {
        double var = field_num(f, n, 10);
        if (has_field(f, n, 11) && (f[11][0] == 'W' || f[11][0] == 'w'))
            var = -var;
        s_data.magvar_deg = var;
        s_data.has_magvar = true;
    }
}

// GSA: solution dimensionality and the PDOP/HDOP/VDOP triplet.
static void parse_gsa(char **f, int n) {
    if (has_field(f, n, 2)) {
        int m = atoi(f[2]);
        s_data.mode = (m == GPS_MODE_2D || m == GPS_MODE_3D) ? (gps_fix_mode_t)m : GPS_MODE_NOFIX;
    }
    if (has_field(f, n, 15)) {
        s_data.pdop = field_num(f, n, 15);
        s_data.has_pdop = true;
    }
    if (has_field(f, n, 16)) {
        s_data.hdop = field_num(f, n, 16);
        s_data.has_hdop = true;
    }
    if (has_field(f, n, 17)) {
        s_data.vdop = field_num(f, n, 17);
        s_data.has_vdop = true;
    }
}

// GSV: satellites in view for the talker that sent it. Every message of a
// talker's cycle repeats the same total in field 3, so the count is taken from
// whichever message arrives without waiting for the cycle to complete.
static void parse_gsv(const char *talker, char **f, int n) {
    if (!has_field(f, n, 3))
        return;
    int in_view = atoi(f[3]);
    if (in_view < 0)
        in_view = 0;
    if (in_view > 255)
        in_view = 255;
    note_sats_in_view(talker, (uint8_t)in_view);
}

// VTG: course over ground and ground speed. Read as a fallback for receivers
// whose RMC leaves those fields blank; the km/h field is taken directly when
// present rather than reconverting the knots one.
static void parse_vtg(char **f, int n) {
    if (has_field(f, n, 1)) {
        s_data.course_deg = field_num(f, n, 1);
        s_data.has_course = true;
    }
    if (has_field(f, n, 7)) {
        s_data.speed_kmh = field_num(f, n, 7);
        s_data.has_speed = true;
    } else if (has_field(f, n, 5)) {
        s_data.speed_kmh = field_num(f, n, 5) * 1.852;
        s_data.has_speed = true;
    }
}

// Dispatches one verified sentence. The talker prefix is the first two
// characters of the identifier and the sentence type the last three, so a
// module emitting $GNRMC, $GPRMC or $BDGSV reaches the same parser.
static void dispatch(char *line) {
    char *fields[GPS_MAX_FIELDS];
    int n = split_fields(line, fields, GPS_MAX_FIELDS);
    if (n < 1)
        return;

    size_t id_len = strlen(fields[0]);
    if (id_len < 5)
        return;

    char talker[3] = { fields[0][0], fields[0][1], 0 };
    const char *type = fields[0] + id_len - 3;

    if (strcmp(type, "GGA") == 0)
        parse_gga(fields, n);
    else if (strcmp(type, "RMC") == 0)
        parse_rmc(fields, n);
    else if (strcmp(type, "GSA") == 0)
        parse_gsa(fields, n);
    else if (strcmp(type, "GSV") == 0)
        parse_gsv(talker, fields, n);
    else if (strcmp(type, "VTG") == 0)
        parse_vtg(fields, n);
}

// Handles one complete line lifted out of the byte stream, counting it as good
// or bad and updating the link timestamp. A sentence that fails its checksum
// still proves the receiver is transmitting, but it is never allowed to reach
// a parser and it does not refresh the link timestamp: a module producing
// nothing but corrupted lines is a wiring or baud-rate problem, and reporting
// its link as up would hide exactly that.
static void handle_line(char *line) {
    gps_lock();
    if (checksum_ok(line)) {
        s_data.sentences_ok++;
        s_last_sentence_s = sched_mono_seconds();
        s_have_sentence = true;
        dispatch(line);
    } else {
        s_data.sentences_bad++;
    }
    gps_unlock();
}

// -------------------------------------------------------------------------
// Reader task
// -------------------------------------------------------------------------

static void gpsTask(void *arg) {
    (void)arg;

    char sentence[GPS_SENTENCE_MAX];
    size_t len = 0;
    bool in_sentence = false;
    bool overflow = false;
    uint8_t chunk[GPS_READ_CHUNK];

    ESP_LOGI(TAG, "GNSS reader started on UART%d (RX=GPIO%d, TX=GPIO%d) at %d baud", (int)GPS_UART_PORT, (int)GPS_UART_RX_GPIO, (int)GPS_UART_TX_GPIO,
             (int)GPS_UART_BAUD);

    for (;;) {
        if (s_stop_request) {
            // Leave the snapshot reading as a receiver that is not there, so
            // anything that samples it between the switch moving and the next
            // page load sees the truth rather than the last fix.
            gps_lock();
            memset(&s_data, 0, sizeof(s_data));
            s_data.mode = GPS_MODE_NOFIX;
            gps_unlock();
            ESP_LOGI(TAG, "GNSS reader stopped");
            s_task_alive = false;
            vTaskDelete(NULL);
        }

        int got = uart_read_bytes((uart_port_t)GPS_UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(GPS_READ_WAIT_MS));

        for (int i = 0; i < got; i++) {
            char c = (char)chunk[i];

            if (c == '$') {
                // A '$' always starts a sentence, even in the middle of one:
                // the only way that happens is that the previous line was cut
                // short by a lost byte, and resynchronising here recovers on
                // the very next sentence instead of merging two of them into
                // one unparseable line.
                len = 0;
                in_sentence = true;
                overflow = false;
                continue;
            }

            if (!in_sentence)
                continue;

            if (c == '\r' || c == '\n') {
                if (overflow) {
                    gps_lock();
                    s_data.sentences_bad++;
                    gps_unlock();
                } else if (len > 0) {
                    sentence[len] = 0;
                    handle_line(sentence);
                }
                len = 0;
                in_sentence = false;
                overflow = false;
                continue;
            }

            if (len + 1 >= sizeof(sentence)) {
                // Keep consuming to the end of the line rather than starting a
                // new sentence mid-stream, so the discard covers exactly the
                // overlong line.
                overflow = true;
                continue;
            }
            sentence[len++] = c;
        }

        // Staleness is evaluated on every pass, not only when bytes arrive:
        // a receiver that has gone silent produces no event of its own, so
        // this is the only place the snapshot can learn it is stale.
        gps_lock();
        int64_t now = sched_mono_seconds();
        s_data.link_up = s_have_sentence && ((now - s_last_sentence_s) < GPS_LINK_TIMEOUT_S);
        if (!s_data.link_up) {
            // With no module talking, nothing that describes a position can
            // still be true. The counters and the ages stay, because those are
            // what an operator needs to tell "never wired up" from "was
            // working and stopped".
            s_data.valid = false;
            s_data.has_position = false;
            s_data.quality = GPS_FIX_NONE;
            s_data.mode = GPS_MODE_NOFIX;
        }
        gps_unlock();
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

// Installs the UART on the configured pins and starts the reader task.
static void gps_bringup(void) {
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            ESP_LOGE(TAG, "Could not create the GNSS lock - receiver disabled");
            return;
        }
    }

    memset(&s_data, 0, sizeof(s_data));
    memset(s_talkers, 0, sizeof(s_talkers));
    s_data.mode = GPS_MODE_NOFIX;
    s_last_sentence_s = 0;
    s_have_sentence = false;
    s_last_fix_s = 0;
    s_have_fix = false;
    s_stop_request = false;

    const uart_config_t cfg = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // No TX ring and no event queue: nothing is ever written to the receiver,
    // and the reader polls with its own timeout instead of waiting on driver
    // events, which is what lets it re-evaluate staleness on a silent line and
    // notice a shutdown request on an idle one.
    esp_err_t err = uart_driver_install((uart_port_t)GPS_UART_PORT, GPS_UART_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install() failed: %s - GNSS receiver disabled", esp_err_to_name(err));
        return;
    }

    err = uart_param_config((uart_port_t)GPS_UART_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config() failed: %s - GNSS receiver disabled", esp_err_to_name(err));
        uart_driver_delete((uart_port_t)GPS_UART_PORT);
        return;
    }

    err = uart_set_pin((uart_port_t)GPS_UART_PORT, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin() failed: %s - GNSS receiver disabled", esp_err_to_name(err));
        uart_driver_delete((uart_port_t)GPS_UART_PORT);
        return;
    }

    s_port_installed = true;
    s_task_alive = true;
    if (xTaskCreate(gpsTask, "gps", GPS_TASK_STACK_BYTES, NULL, GPS_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create the GNSS reader task - receiver disabled");
        s_task_alive = false;
        uart_driver_delete((uart_port_t)GPS_UART_PORT);
        s_port_installed = false;
        return;
    }
}

// Asks the reader task to exit, waits for it, then releases the UART.
// A timeout here does not retract s_stop_request: the task may already have
// observed it and be on its way out, and un-asking now would leave that exit
// racing a fresh bring-up. The request stays latched so a task that finishes
// late still runs its own exit path and lowers s_task_alive, and the next
// call in either direction re-reads the live s_task_alive/s_port_installed
// state instead of trusting a flag that could have gone stale in the meantime.
static void gps_teardown(void) {
    s_stop_request = true;

    for (int waited = 0; s_task_alive && waited < GPS_STOP_TIMEOUT_MS; waited += GPS_STOP_POLL_MS)
        vTaskDelay(pdMS_TO_TICKS(GPS_STOP_POLL_MS));

    if (s_task_alive) {
        // The task did not answer within the timeout, so the port stays
        // installed for now: releasing the driver here would pull it out
        // from under a task that may still be reading it. s_stop_request
        // stays set so that whenever the task does get to run again it takes
        // the exit path and lowers s_task_alive on its own; the next
        // gps_apply_config() call will see that and finish the teardown then,
        // rather than believing the receiver is enabled because s_task_alive
        // never got a chance to come down before someone asked.
        ESP_LOGE(TAG, "GNSS reader did not stop within %d ms - will finish teardown once it does", (int)GPS_STOP_TIMEOUT_MS);
        return;
    }

    uart_driver_delete((uart_port_t)GPS_UART_PORT);
    s_port_installed = false;
    s_stop_request = false;
}

void gps_apply_config(void) {
    bool want = g_config.gps_en;
    bool running = s_task_alive;

    if (!running && s_port_installed) {
        // The reader task has exited (on request, or otherwise) but the port
        // is still installed - either a teardown that was still waiting on a
        // slow task now has its answer, or the task died unexpectedly. Either
        // way this is not a running receiver: finish releasing the port so
        // the state below reflects reality instead of the stale "enabled and
        // silent" condition this whole function exists to prevent.
        uart_driver_delete((uart_port_t)GPS_UART_PORT);
        s_port_installed = false;
        s_stop_request = false;
    }

    if (want && !running)
        gps_bringup();
    else if (!want && running)
        gps_teardown();
}

bool gps_enabled(void) {
    return s_task_alive;
}

bool gps_snapshot(gps_data_t *out) {
    if (out == NULL)
        return false;

    if (!s_task_alive || s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        out->mode = GPS_MODE_NOFIX;
        return false;
    }

    gps_lock();
    *out = s_data;
    int64_t now = sched_mono_seconds();
    out->has_link_age = s_have_sentence;
    out->link_age_s = s_have_sentence ? (uint32_t)(now - s_last_sentence_s) : 0;
    out->has_fix_age = s_have_fix;
    out->fix_age_s = s_have_fix ? (uint32_t)(now - s_last_fix_s) : 0;
    gps_unlock();
    return true;
}
