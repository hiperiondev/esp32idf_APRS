/**
 * @file aprs_coord.h
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
 * @brief Shared APRS position and symbol conversion.
 *
 * On the transmit side, decimal-degrees -> APRS position field conversion for
 * every module that builds a position report (station beacons, Objects/Items,
 * weather reports): the uncompressed "DDMM.mmN/DDDMM.mmW" field pair (with
 * optional position ambiguity, APRS101 chapter 6), the base-91 compressed
 * position format (APRS101 chapter 9), and the Maidenhead grid locator used
 * by status reports (APRS101 chapter 16).
 *
 * On the receive side, the decoders that read a position or a symbol out of a
 * packet whose own information field does not spell them out in one of those
 * formats: raw NMEA-0183 sentences behind the '$' data type identifier
 * (APRS101 chapter 6) and symbols carried in the AX.25 destination address or
 * source SSID (APRS101 chapters 20 and 21).
 */

#ifndef APRS_COORD_H
#define APRS_COORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Length of the Maidenhead grid locator aprs_maidenhead_locator()
 * writes, not counting its NUL terminator ("IO91SX" - field, square and
 * subsquare).
 */
#define APRS_MAIDENHEAD_LEN 6

/**
 * @brief Buffer size a caller must provide for aprs_maidenhead_locator().
 */
#define APRS_MAIDENHEAD_BUF_SIZE (APRS_MAIDENHEAD_LEN + 1)

/**
 * @brief Highest position-ambiguity level accepted by
 * aprs_coord_format_ambiguous() (0 = full precision, 4 = nearest degree).
 */
#define APRS_COORD_AMBIGUITY_MAX 4

/**
 * @brief Format a decimal-degrees latitude/longitude pair as the APRS
 * uncompressed position fields "DDMM.mmN"/"S" and "DDDMM.mmE"/"W".
 *
 * The minutes value is rounded to two decimal places before the
 * degrees/minutes split is finalized, so a minutes value that rounds up to
 * 60.00 carries into the degrees field instead of being emitted as-is.
 * This keeps the output within the valid MM.mm range of 00.00-59.99
 * required by the APRS spec.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param latOut Destination buffer for the latitude field.
 * @param latMax Size of latOut.
 * @param lonOut Destination buffer for the longitude field.
 * @param lonMax Size of lonOut.
 */
void aprs_coord_format(float lat, float lon, char *latOut, size_t latMax, char *lonOut, size_t lonMax);

/**
 * @brief Format a decimal-degrees latitude/longitude pair as the APRS
 * uncompressed position fields with position ambiguity applied (APRS101
 * chapter 6).
 *
 * Ambiguity is expressed on air by replacing the least significant digits of
 * the minutes with spaces, so a receiver knows the position is deliberately
 * imprecise instead of merely rounded:
 *
 *   0 - "4903.50N" / "07201.75W"  full precision (hundredths of a minute)
 *   1 - "4903.5 N" / "07201.7 W"  nearest 1/10 minute
 *   2 - "4903.  N" / "07201.  W"  nearest minute
 *   3 - "490 .  N" / "0720 .  W"  nearest 10 minutes
 *   4 - "49  .  N" / "072  .  W"  nearest degree
 *
 * The decimal point, the hemisphere character and the overall field widths are
 * unchanged at every level, which is what keeps the report parseable. Digits
 * are blanked, never rounded away, matching the reference decoders that read
 * the blanked field as "unknown digit".
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param ambiguity Ambiguity level 0..::APRS_COORD_AMBIGUITY_MAX; values above
 *        the maximum are clamped to it.
 * @param latOut Destination buffer for the latitude field.
 * @param latMax Size of latOut.
 * @param lonOut Destination buffer for the longitude field.
 * @param lonMax Size of lonOut.
 */
void aprs_coord_format_ambiguous(float lat, float lon, uint8_t ambiguity, char *latOut, size_t latMax, char *lonOut, size_t lonMax);

/**
 * @brief Build the 6-character Maidenhead grid locator ("IO91SX") for a
 * decimal-degrees position.
 *
 * Field letters are uppercase ('A'..'R'), square digits are '0'..'9' and
 * subsquare letters are uppercase ('A'..'X') - the case convention APRS
 * status reports use on air (APRS101 chapter 16). Latitude and longitude are
 * clamped to the poles and the antimeridian first, so an out-of-range input
 * still yields a valid locator rather than running off the end of the
 * alphabet.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param out Destination buffer, NUL-terminated on return.
 * @param outMax Size of @p out; must be at least
 *        ::APRS_MAIDENHEAD_BUF_SIZE for the full locator. A smaller buffer
 *        receives as much of the locator as fits, still terminated.
 */
void aprs_maidenhead_locator(float lat, float lon, char *out, size_t outMax);

/**
 * @brief Symbol Table Identifier used when the configured one is not a valid
 * identifier at all: the primary table.
 */
#define APRS_SYMBOL_TABLE_DEFAULT '/'

/**
 * @brief Lowest byte accepted as a Symbol Code, i.e. the first printable
 * ASCII character.
 */
#define APRS_SYMBOL_CODE_MIN '!'

/**
 * @brief Highest byte accepted as a Symbol Code, i.e. the last printable
 * ASCII character.
 */
#define APRS_SYMBOL_CODE_MAX '~'

/**
 * @brief Symbol Code used when the configured one is outside
 * ::APRS_SYMBOL_CODE_MIN ..::APRS_SYMBOL_CODE_MAX : the primary-table
 * diamond ('&') this firmware beacons with by default.
 */
#define APRS_SYMBOL_CODE_DEFAULT '&'

/**
 * @brief First byte of the lower-case range a *compressed* position report
 * carries a numeric overlay in, i.e. the byte that stands for overlay '0'.
 *
 * APRS 1.2 chapter 21 allows the Symbol Table Identifier of an *uncompressed*
 * report to be '/', '\\', 'A'-'Z' or '0'-'9', but forbids the numeric form in
 * a compressed report: a compressed position field never starts with a digit,
 * because that first byte is exactly how a receiver tells the two layouts
 * apart. A numeric overlay therefore travels as the matching lower-case
 * letter, 'a' for '0' through 'j' for '9', and is mapped back to the digit on
 * receive.
 *
 * aprs_coord_format_compressed() applies the mapping itself, so a caller
 * passes the configured Symbol Table Identifier unchanged whichever layout it
 * is building.
 */
#define APRS_COMPRESSED_OVERLAY_DIGIT_BASE 'a'

/**
 * @brief Test whether a byte is a Symbol Table Identifier (APRS 1.2 chapter
 * 21): the primary table '/', the alternate table '\\', an alphabetic
 * overlay 'A'-'Z' or a numeric overlay '0'-'9'.
 *
 * This is the configured form of the byte, the one the web form and the
 * configuration file carry. The numeric overlays it accepts are legal on air
 * only in an uncompressed report; aprs_coord_format_compressed() maps them
 * onto ::APRS_COMPRESSED_OVERLAY_DIGIT_BASE for the compressed layout.
 *
 * @param t Byte to test.
 * @return true when @p t is a valid Symbol Table Identifier.
 */
static inline bool aprs_symbol_table_is_valid(char t) {
    return t == '/' || t == '\\' || (t >= 'A' && t <= 'Z') || (t >= '0' && t <= '9');
}

/**
 * @brief Test whether a byte is a Symbol Code, i.e. a printable ASCII
 * character in the ::APRS_SYMBOL_CODE_MIN ..::APRS_SYMBOL_CODE_MAX range the
 * symbol tables are indexed by.
 *
 * @param c Byte to test.
 * @return true when @p c is a valid Symbol Code.
 */
static inline bool aprs_symbol_code_is_valid(char c) {
    return c >= APRS_SYMBOL_CODE_MIN && c <= APRS_SYMBOL_CODE_MAX;
}

/**
 * @brief Format a decimal-degrees latitude/longitude pair as the APRS
 * base-91 compressed position field, per APRS101 chapter 9: symbol-table
 * byte, 4 compressed-latitude digits, 4 compressed-longitude digits, symbol
 * code, and the 3-byte compression type/course-speed token.
 *
 * @param lat Latitude in decimal degrees (positive = N, negative = S).
 * @param lon Longitude in decimal degrees (positive = E, negative = W).
 * @param symTable Symbol Table Identifier as configured: '/' primary,
 *        '\\' alternate, an alphabetic overlay 'A'-'Z' or a numeric overlay
 *        '0'-'9'. A numeric overlay is mapped onto the lower-case byte the
 *        compressed layout requires (see
 *        ::APRS_COMPRESSED_OVERLAY_DIGIT_BASE); anything that is not a
 *        Symbol Table Identifier at all is refused and replaced with
 *        ::APRS_SYMBOL_TABLE_DEFAULT, since a digit in this position turns
 *        the whole report into an uncompressed one for every receiver.
 * @param symCode Symbol code byte.
 * @param csT The 3-byte token built by aprs_compressed_cs_from_course_speed(),
 *        aprs_compressed_cs_from_range() or aprs_compressed_cs_from_altitude()
 *        - the three readings of these bytes the spec defines - or "   " (3
 *        spaces) to emit "no cs/T data" per spec.
 * @param out Destination buffer for the compressed position field (symbol
 *        table byte + 4 lat digits + 4 lon digits + symbol code + 3-byte csT).
 * @param outMax Size of out; must be >= 14 to hold the full 13-byte field plus
 *        its NUL. A smaller buffer is not an error - the field is copied as
 *        far as it fits and always terminated - but a truncated compressed
 *        position still decodes, as a *different* coordinate, so any caller
 *        that cannot guarantee 14 bytes here puts wrong positions on the air.
 */
void aprs_coord_format_compressed(float lat, float lon, char symTable, char symCode, const char csT[3], char *out, size_t outMax);

/**
 * @brief ASCII offset applied to every base-91 digit of a compressed position
 * field, per APRS101 chapter 9: numeric value 0 is transmitted as '!' (33).
 */
#define APRS_COMPRESSED_BASE91_OFFSET 33

/**
 * @brief Highest numeric value either byte of the compressed course/speed
 * token may carry, i.e. ASCII 'z'.
 *
 * APRS101 chapter 9 gives the course/speed form the numeric range 0-89
 * ('!' through 'z') and reserves the next value, 90 ('{'), for the
 * pre-calculated radio range form of the same two bytes. A course/speed
 * token whose first byte reached 90 would therefore be decoded as a range
 * circle rather than as a moving station.
 */
#define APRS_COMPRESSED_CS_DIGIT_MAX 89

/**
 * @brief Build the 2-byte compressed course/speed token described by
 * APRS101 chapter 9, followed by the fixed compression-type byte for
 * "compressed Course/Speed, current GPS fix".
 *
 * A receiver decodes the two bytes as `course = c * 4` degrees and
 * `speed = 1.08^s - 1` knots, where c and s are the numeric values of the
 * bytes once ::APRS_COMPRESSED_BASE91_OFFSET has been subtracted. This
 * function is the exact inverse of that pair of relations:
 *
 *   - Course is truncated to the 4 degree step the field quantises to, so
 *     the value read back is always the multiple of 4 at or below the course
 *     given. Due north is digit 0 and decodes as 0 degrees.
 *   - Speed is `round(log(1 + speed) / log(1.08))`, which reproduces the
 *     requested speed to within the roughly 8 % step of the table.
 *
 * Both digits are capped at ::APRS_COMPRESSED_CS_DIGIT_MAX, so the token can
 * never be mistaken for the pre-calculated radio range form. A speed beyond
 * the top of the table saturates at approximately 940 knots.
 *
 * @param course_deg Course over ground, degrees; values of 360 and above are
 *        reduced modulo 360.
 * @param speed_knots Speed over ground, knots.
 * @param out 3-byte destination for the course/speed-and-type token.
 */
void aprs_compressed_cs_from_course_speed(unsigned course_deg, unsigned speed_knots, char out[3]);

/**
 * @brief Compression type (T) byte emitted with the course/speed and
 * pre-calculated radio range forms of the cs bytes.
 *
 * APRS101 chapter 9 packs three fields into the T byte: the GPS fix status
 * (bit 5), the NMEA sentence the position came from (bits 4-3) and the origin
 * of the compression (bits 2-0). This value is 34 (0b100010) plus the
 * ::APRS_COMPRESSED_BASE91_OFFSET, i.e. current fix, no NMEA source, software
 * compression origin - what a station computing the field itself from a
 * configured position reports.
 */
#define APRS_COMPRESSED_T_BYTE_CS 'C'

/**
 * @brief Compression type (T) byte emitted with the altitude form of the cs
 * bytes.
 *
 * Same fields as ::APRS_COMPRESSED_T_BYTE_CS with the NMEA source set to GGA
 * (bits 4-3 = 0b10, adding 16 to the value), which is what tells a receiver to
 * read the two cs bytes as an altitude instead of as a course/speed pair.
 * There is no other way to select the altitude form: the two bytes carry no
 * marker of their own.
 */
#define APRS_COMPRESSED_T_BYTE_ALTITUDE 'S'

/**
 * @brief Bit mask selecting the NMEA source field of the compression type
 * (T) byte, once ::APRS_COMPRESSED_BASE91_OFFSET has been subtracted
 * (APRS101 chapter 9).
 *
 * The field is two bits wide and names the sentence the position came from:
 * 0 = other, 1 = GLL, 2 = GGA, 3 = RMC. It is what tells a receiver whether
 * the cs pair holds an altitude, since only a GGA fix carries one.
 */
#define APRS_COMPRESSED_T_NMEA_MASK 0x18

/**
 * @brief Value of the ::APRS_COMPRESSED_T_NMEA_MASK field, already shifted
 * down, that marks a GGA source and therefore an altitude in the cs pair.
 */
#define APRS_COMPRESSED_T_NMEA_GGA 2

/**
 * @brief Bit position of the ::APRS_COMPRESSED_T_NMEA_MASK field within the
 * compression type byte.
 */
#define APRS_COMPRESSED_T_NMEA_SHIFT 3

/**
 * @brief Numeric value of the first compressed cs byte that selects the
 * pre-calculated radio range form, i.e. ASCII '{'.
 *
 * APRS101 chapter 9 reserves this one value, immediately above the
 * ::APRS_COMPRESSED_CS_DIGIT_MAX the course/speed form ends at, to mark the
 * two bytes as a range circle rather than a moving station.
 */
#define APRS_COMPRESSED_CS_RANGE_MARKER 90

/**
 * @brief Build the 2-byte compressed pre-calculated radio range token
 * described by APRS101 chapter 9, followed by the fixed compression-type
 * byte.
 *
 * A receiver decodes the two bytes as `range = 2 * 1.08^s` statute miles,
 * where the first byte is the fixed '{' marker
 * (::APRS_COMPRESSED_CS_RANGE_MARKER plus ::APRS_COMPRESSED_BASE91_OFFSET)
 * and s is the numeric value of the second byte once
 * ::APRS_COMPRESSED_BASE91_OFFSET has been subtracted. This function is the
 * exact inverse of that relation: `s = round(log(range / 2) / log(1.08))`,
 * which reproduces the requested range to within the roughly 8 % step of the
 * table. Ranges below the 2 mile floor the form can express encode as s = 0,
 * and ranges past the top of the table saturate at
 * ::APRS_COMPRESSED_CS_DIGIT_MAX, which stands for about 1890 miles.
 *
 * This is the compressed equivalent of the uncompressed "RNGrrrr" data
 * extension: a beacon emits one or the other, never both, since they are two
 * encodings of the same figure.
 *
 * @param range_miles Omnidirectional radio range, statute miles.
 * @param out 3-byte destination for the radio-range-and-type token.
 */
void aprs_compressed_cs_from_range(unsigned range_miles, char out[3]);

/**
 * @brief Highest combined numeric value the two compressed cs bytes can carry
 * in the altitude form, i.e. both bytes at ASCII '{'.
 *
 * The altitude form reads the two bytes as the single number `c * 91 + s`, so
 * unlike the course/speed form it uses the whole base-91 range of both bytes -
 * including the value 90 that the course/speed form has to stay clear of. The
 * ceiling is therefore 90 * 91 + 90.
 */
#define APRS_COMPRESSED_ALT_CS_MAX 8280

/**
 * @brief Build the 2-byte compressed altitude token described by APRS101
 * chapter 9, followed by the ::APRS_COMPRESSED_T_BYTE_ALTITUDE compression
 * type byte that selects this reading of them.
 *
 * A receiver decodes the two bytes as `altitude = 1.002^(c * 91 + s)` feet,
 * where c and s are the numeric values of the bytes once
 * ::APRS_COMPRESSED_BASE91_OFFSET has been subtracted. This function is the
 * exact inverse of that relation: `cs = round(log(altitude) / log(1.002))`,
 * split into `c = cs / 91` and `s = cs % 91`, which reproduces the requested
 * altitude to within the roughly 0.2 % step of the table. Altitudes at or
 * below the 1 foot floor the form can express encode as cs = 0, which reads
 * back as 1 foot, and altitudes past the top of the table saturate at
 * ::APRS_COMPRESSED_ALT_CS_MAX.
 *
 * This is the compressed equivalent of the uncompressed "/A=" comment token:
 * a report carries one or the other, never both, since they are two encodings
 * of the same figure. It costs nothing on air - the three bytes it occupies
 * are part of every compressed position field - where "/A=" costs nine.
 *
 * @param alt_feet Altitude above mean sea level, feet.
 * @param out 3-byte destination for the altitude-and-type token.
 */
void aprs_compressed_cs_from_altitude(unsigned alt_feet, char out[3]);

/**
 * @brief Extract the symbol table identifier and symbol code from a
 * (non-Mic-E) APRS position/object/item info field, covering:
 *
 *   - Position reports: '!' and '=' (no timestamp) or '/' and '@' (7-byte
 *     DHM/HMS timestamp between the DTI and the position field).
 *   - Object reports (';' DTI, APRS101 chapter 11): 9-byte name, 1-byte
 *     live('*')/killed('_') flag, 7-byte DHM timestamp, then the position
 *     field.
 *   - Item reports (')' DTI, APRS101 chapter 11): 3-9 byte name terminated
 *     by a 1-byte live('!')/killed('_') flag, then the position field.
 *
 * Each of those ends in a position field in either of the two layouts
 * defined by APRS101 chapter 9: the uncompressed "DDMM.mmN/DDDMM.mmW" pair
 * (symbol table byte after the 8-byte latitude, symbol code after the
 * 9-byte longitude) or the base-91 compressed field (symbol table byte
 * immediately at the start of the field, symbol code 9 bytes later). The
 * two position layouts are told apart by the first byte of the field: a
 * decimal digit means uncompressed, '/' or '\\' means compressed.
 *
 * @param info Pointer to the start of the AX.25/TNC2 Information field
 *        (i.e. starting at the DTI byte itself).
 * @param infoLen Number of valid bytes available at info (does not need to
 *        be NUL-terminated; pass strlen(info) for a NUL-terminated line).
 * @param symTable Out param: set to the symbol table byte ('/' primary,
 *        '\\' alternate, or an overlay character) on success, left
 *        untouched on failure.
 * @param symCode Out param: set to the symbol code byte on success, left
 *        untouched on failure.
 * @return true if info is a recognized position/object/item DTI, in either
 *         the compressed or uncompressed position layout, and long enough
 *         to contain the symbol pair; false otherwise (symTable/symCode are
 *         left untouched, so callers should zero-initialize them before
 *         calling).
 */
bool aprs_extract_symbol(const char *info, size_t infoLen, char *symTable, char *symCode);

/**
 * @brief Decode a raw NMEA-0183 position sentence carried behind the APRS
 * '$' data type identifier (APRS101 chapter 6, "Raw NMEA Position Reports",
 * and Appendix 1).
 *
 * Recognized sentences are @c RMC (recommended minimum), @c GGA (fix data)
 * and @c GLL (geographic position). Any two-letter talker identifier is
 * accepted, not just @c GP: multi-constellation receivers emit @c GN, @c GL,
 * @c GA and @c GB for the same sentences, and APRS 1.2c renamed GPS to GNSS
 * throughout for exactly that reason.
 *
 * A sentence is rejected when its optional "*HH" checksum trailer is present
 * and does not match, when @c RMC reports the navigation-warning status @c V
 * instead of @c A, when @c GGA reports fix quality 0 ("fix not available"),
 * when @c GLL carries the later status field and it reads @c V, or when any
 * coordinate field is malformed or out of range. A corrupt or stale position
 * is worse than none at all for the range gate this feeds, so there is no
 * best-effort fallback.
 *
 * @c $GPWPL is deliberately not decoded: it names a waypoint rather than the
 * transmitting station's own fix, so treating it as a position report would
 * range-gate a station on a coordinate it is not at.
 *
 * The whole parse is integer arithmetic - coordinates are accumulated in
 * units of 1e-7 degrees and converted to float exactly once on return - so
 * the result does not depend on floating-point rounding of the input text.
 *
 * @param sentence First byte of the sentence, which is the '$' itself (the
 *        APRS data type identifier and the NMEA start delimiter are the same
 *        character).
 * @param len Number of valid bytes available at @p sentence. The sentence
 *        does not need to be NUL-terminated; a trailing CR and/or LF is
 *        ignored.
 * @param outLat Out param: latitude in decimal degrees (positive = N),
 *        written only on success.
 * @param outLon Out param: longitude in decimal degrees (positive = E),
 *        written only on success.
 * @return true if a supported sentence was decoded to a valid position.
 */
bool aprs_nmea_decode_position(const char *sentence, size_t len, float *outLat, float *outLon);

/**
 * @brief Resolve the display symbol a packet carries outside its information
 * field (APRS101 chapters 20 and 21).
 *
 * This is the second and third steps of the symbol precedence order the
 * project implements:
 *
 *   1. the information field, read by aprs_extract_symbol(); it always wins;
 *   2. the AX.25 destination address, in the forms "GPSxyz", "SPCxyz" and
 *      "SYMxyz" (the three prefixes are equivalent) or the numeric forms
 *      "GPSCnn" (primary table) and "GPSEnn" (alternate table), where the
 *      optional overlay character @c z replaces the alternate table byte;
 *   3. the source address SSID, which selects one of fifteen symbols on the
 *      primary table.
 *
 * Step 3 is legacy and is applied only to the raw NMEA data type identifier
 * '$', which is the one case the convention was invented for. Applying it
 * more widely would turn any ordinary "-9" station's messages and status
 * reports into cars.
 *
 * Mic-E packets are skipped entirely: their destination address carries
 * latitude, message code and ambiguity, so reading a symbol out of it would
 * be reading position data.
 *
 * @param dti Data type identifier of the packet, i.e. the first byte of its
 *        information field.
 * @param destCall Destination callsign. A trailing "-SSID" is ignored.
 * @param srcSsid Source address SSID, 0 when the source carries none.
 * @param symTable Out param: symbol table byte ('/' primary, '\\' alternate,
 *        or an overlay character) on success, left untouched on failure.
 * @param symCode Out param: symbol code byte on success, left untouched on
 *        failure.
 * @return true if a symbol was resolved.
 */
bool aprs_symbol_from_dest(char dti, const char *destCall, int srcSsid, char *symTable, char *symCode);

/**
 * @brief aprs_symbol_from_dest() for a packet available as TNC2 text
 * ("SRC-N>DEST-N,PATH,...:info") rather than as a decoded AX.25 frame.
 *
 * Reads the destination callsign and the source SSID out of the header and
 * applies the same precedence rules. Callers pass the data type identifier
 * separately because the information field they already searched starts
 * after the first ':' of the same line.
 *
 * @param line Complete TNC2 line, NUL-terminated.
 * @param dti Data type identifier of the packet.
 * @param symTable Out param: symbol table byte on success, left untouched on
 *        failure.
 * @param symCode Out param: symbol code byte on success, left untouched on
 *        failure.
 * @return true if a symbol was resolved.
 */
bool aprs_symbol_from_tnc2_header(const char *line, char dti, char *symTable, char *symCode);

/**
 * @brief Copy the destination callsign out of a TNC2 line
 * ("SRC-N>DEST-N,PATH,...:info").
 *
 * The destination runs from the '>' up to the first ',' or ':' that follows
 * it, and any "-SSID" it carries is part of what is copied. Mic-E is the
 * reason this is worth having on its own: its destination address is a data
 * field, not a callsign, so a receiver has to hand it to the Mic-E decoder
 * together with the information field.
 *
 * @param line Complete TNC2 line, NUL-terminated.
 * @param out Buffer for the NUL-terminated destination field.
 * @param out_max Size of @p out in bytes. A destination longer than this
 *        fits nothing and is refused rather than truncated, since a partial
 *        Mic-E destination decodes to a wrong position.
 * @return true if a destination field was found and copied whole.
 */
bool aprs_tnc2_dest_call(const char *line, char *out, size_t out_max);

#endif
