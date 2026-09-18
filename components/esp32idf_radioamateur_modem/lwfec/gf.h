/**
 * @file gf.h
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
 * @brief Galois field GF(2^8) arithmetic (add, subtract, multiply, divide,
 * power) used by the Reed-Solomon codec.
 */

#ifndef GF_H_
#define GF_H_

#include <stdint.h>

/**
 * @brief Antilogarithm table, two full cycles of the field
 * @note The 255 elements of one cycle are stored twice, so every index in
 *       0..509 is valid and log sums or differences need no reduction.
 */
extern const uint8_t GfExp[510];

/**
 * @brief Logarithm table
 */
extern const uint8_t GfLog[256];

/**
 * @brief Add in Galois field
 * @param x Term 1
 * @param y Term 2
 * @return Sum
 */
static inline uint8_t GfAdd(uint8_t x, uint8_t y) {
    return x ^ y;
}

/**
 * @brief Subtract in Galois field
 * @param x Minuend
 * @param y Subtrahend
 * @return Difference
 */
static inline uint8_t GfSub(uint8_t x, uint8_t y) {
    return x ^ y;
}

/**
 * @brief Multiply in Galois field
 * @param x Multiplicand
 * @param y Multiplier
 * @return Multiplication result
 * @note Uses the log/antilog tables: since log(x)+log(y)=log(x*y) and
 *       b^log(a)=a, x*y = GfExp[GfLog[x]+GfLog[y]]. The sum peaks at 508 and
 *       ::GfExp holds two cycles, so no reduction is needed. Multiplication by
 *       0 is handled explicitly.
 */
static inline uint8_t GfMul(uint8_t x, uint8_t y) {
    if ((x == 0) || (y == 0))
        return 0;
    return GfExp[GfLog[x] + GfLog[y]];
}

/**
 * @brief Divide in Galois field
 * @param dividend Dividend
 * @param divisor Divisor
 * @return Division result. 0 is returned when dividing by 0.
 * @note Mirrors ::GfMul via the log tables: x/y = GfExp[255 + GfLog[x] -
 *       GfLog[y]]. The index stays within 1..509. Division by 0 returns 0.
 */
static inline uint8_t GfDiv(uint8_t dividend, uint8_t divisor) {
    if (divisor == 0)
        return 0;
    if (dividend == 0)
        return 0;
    return GfExp[255 + GfLog[dividend] - GfLog[divisor]];
}

/**
 * @brief Exponentiate in Galois field
 * @param x Base
 * @param exponent Exponent
 * @return Result
 * @note Via the log tables: x^a = GfExp[(a*GfLog[x]) mod 255], since
 *       a*log(x)=log(x^a).
 */
static inline uint8_t GfPow(uint8_t x, uint8_t exponent) {
    return GfExp[(exponent * GfLog[x]) % 255];
}

/**
 * @brief Calculate 2^x in Galois field
 * @param exponent Exponent (x)
 * @return Result
 * @note Reads ::GfExp directly. Any uint8_t exponent is a valid index because
 *       the table spans two cycles.
 */
static inline uint8_t GfPow2(uint8_t exponent) {
    return GfExp[exponent];
}

/**
 * @brief Invert in Galois field
 * @param x Number to calculate the inverse of. Must be non-zero: 0 has no
 *        inverse in the field and the value returned for it is meaningless
 *        (the read itself stays in bounds).
 * @return 1/x
 * @note 1/x = GfExp[255 - GfLog[x]]. The index reaches 255 for x = 1, which the
 *       second cycle of ::GfExp covers, so GfInv(1) yields 1.
 */
static inline uint8_t GfInv(uint8_t x) {
    return GfExp[255 - GfLog[x]];
}

/**
 * @brief Multiply polynomial by a scalar
 * @param *p Input polynomial
 * @param o Length of polynomial buffer (degree of a poly + 1)
 * @param s Scalar multiplicand
 * @param out Ouput polynomial. Has the same degree as input polynomial
 */
void GfPolyScale(const uint8_t *p, uint8_t o, uint8_t s, uint8_t *out);

/**
 * @brief Add two polynomials
 * @param *p1 1st polynomial
 * @param o1 1st polynomial buffer length (degree of a poly + 1)
 * @param *p2 2nd polynomial
 * @param o2 2nd polynomial buffer length (degree of a poly + 1)
 * @param *out Output polynomial buffer, at least max(o1, o2) bytes long
 * @return Output polynomial length, that is max(o1, o2).
 * @note Both branches write the full result: the first min(o1, o2)
 *       coefficients are always p1[i] ^ p2[i], and the remaining tail, up to
 *       max(o1, o2), is copied verbatim from whichever operand is longer.
 *       The function is therefore correct for any pair of buffers and does
 *       not depend on out aliasing either input.
 */
uint8_t GfPolyAdd(const uint8_t *p1, uint8_t o1, const uint8_t *p2, uint8_t o2, uint8_t *out);

/**
 * @brief Multiply two polynomials
 * @param *p1 1st polynomial
 * @param o1 1st polynomial buffer length (degree of a poly + 1)
 * @param *p2 2nd polynomial
 * @param o2 2nd polynomial buffer length (degree of a poly + 1)
 * @param out Output polynomial. Has a length of o1 + o2 - 1
 * @warning Output buffer must be separate from input buffers
 */
void GfPolyMul(const uint8_t *p1, uint8_t o1, const uint8_t *p2, uint8_t o2, uint8_t *out);

/**
 * @brief Evaluate the polynomial at given x
 * @param *p Polynomial
 * @param o Polynomial buffer length (degree of a poly + 1)
 * @param x Value to evaluate the polynomial at
 * @return Evaluated value
 */
uint8_t GfPolyEval(const uint8_t *p, uint8_t o, uint8_t x);

/**
 * @brief Divide two polynomials in Galois field
 * @param *p1 Divident polynomial
 * @param o1 Divident polynomial buffer length
 * @param *p2 Divisor polynomial
 * @param o2 Divisor polynomial buffer length
 * @param *out Output polynomial quotient followed by remainder. Must be
 *             preallocated and at least o1 bytes long: the division is done in
 *             place on a copy of p1, so no index past o1-1 is ever written.
 * @warning This function works on polynomials ordered highest-degree-term-first
 * @return Pointer to the first element of the remainder, that is
 *         &out[o1 - o2 + 1]. The remainder is o2 - 1 bytes long.
 */
uint8_t *GfPolyDiv(const uint8_t *p1, uint8_t o1, const uint8_t *p2, uint8_t o2, uint8_t *out);

/**
 * @brief Reverse the order of elements in a polynomial (in-place)
 * @param *p Polynomial input/output
 * @param o Polynomial buffer length (order + 1)
 */
void GfPolyInv(uint8_t *p, uint8_t o);

#endif