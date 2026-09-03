// @file rs.c
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
// @brief Reed-Solomon FEC codec implementation: syndrome calculation,
// Berlekamp-Massey error locator search, Chien search and Forney error
// correction, written to avoid heap and large stack allocations.

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "gf.h"
#include "rs.h"

// This implementation aims for:
// 1. Minimal RAM usage
// 2. No malloc() (no heap usage)
// 3. No big stack allocated arrays
// All arrays used internally by functions are either declared as static
// or they use the common buffer declared below. This buffer must be used with caution.
// All functions that use this buffer can only use it to store function-scope data.
static uint8_t commonBuffer[4 * RS_MAX_REDUNDANCY_BYTES + 4];

// @brief Calculates message syndromes
// @param *rs RS instance
// @param data Input block (length = N)
// @param size Block size = N
// @param out Output syndromes (length = T)
static void syndromes(const struct LwFecRS *rs, const uint8_t *data, uint8_t size, uint8_t *out) {
    for (uint8_t i = 0; i < rs->T; i++) {
        out[i] = GfPolyEval(data, size, GfPow2(i + rs->fcr));
    }
}

// @brief Calculate the error evaulator (list of erroneous positions)
// @param *locator Error locator polynomial
// @param locatorSize Error locator polynomial length <= T
// @param out List of erroneous positions (error evaulator) (length = locatorSize - 1)
// @return True on success, else the "out" buffer must be invalidated and the block is uncorrectable
static bool errorEvaluator(const uint8_t *locator, uint8_t locatorSize, uint8_t *out) {
    // The roots of the error locator polynomial give the error positions. They are
    // found with a Chien search: the polynomial is evaluated at 2^i for every
    // position of the block, which reuses the log tables instead of doing a full
    // Horner evaluation per position.
    // A zero coefficient contributes nothing to the sum and is skipped: GfLog[]
    // has no entry for 0, so feeding it to the log-domain term would add 2^(i*j)
    // instead.
    uint8_t pos = 0;
    for (uint8_t i = 0; i < RS_BLOCK_SIZE; i++) {
        uint8_t lambda = 0;
        for (uint8_t j = 0; j < locatorSize; j++) {
            uint8_t coeff = locator[locatorSize - j - 1];
            if (coeff == 0)
                continue;

            lambda ^= GfPow2((GfLog[coeff] + i * j) % 255);
        }
        if (lambda == 0) {
            // more roots than the degree of the locator means the locator is not
            // a valid error locator, so stop and report the block as uncorrectable
            if (pos >= (locatorSize - 1)) {
                pos++;
                break;
            }
            out[pos++] = RS_BLOCK_SIZE - i - 1;
        }
    }

    if (pos != (locatorSize - 1))
        return false;

    return true;
}

// @brief Calculates the error locator polynomial
// @param *rs RS instance
// @param *syndromes Syndrome polynomial (length = T)
// @param *out Output error locator buffer, at least T + 1 bytes long
// @param outSize Receives the error locator polynomial length, <= T + 1
// @return True if success, else the "out" buffer must be invalidated and the block is uncorrectable
static bool errorLocator(const struct LwFecRS *rs, const uint8_t *syndromes, uint8_t *out, uint8_t *outSize) {
    // The error locator polynomial is calculated with the Berlekamp-Massey
    // algorithm, ported from the Python listing of the Wikiversity article
    // "Reed-Solomon codes for coders".
    // The four working polynomials live in the common buffer, one
    // RS_MAX_REDUNDANCY_BYTES + 1 slot each.
    uint8_t *errLoc = commonBuffer;
    uint8_t *newLoc = errLoc + RS_MAX_REDUNDANCY_BYTES + 1;
    uint8_t *oldLoc = newLoc + RS_MAX_REDUNDANCY_BYTES + 1;
    uint8_t *tmpLoc = oldLoc + RS_MAX_REDUNDANCY_BYTES + 1;

    memset(errLoc, 0, rs->T + 1);
    memset(newLoc, 0, rs->T + 1);
    memset(oldLoc, 0, rs->T + 1);
    memset(tmpLoc, 0, rs->T + 1);

    uint8_t newLocLen = 0;
    uint8_t errLocLen = 1;
    uint8_t oldLocLen = 1;

    errLoc[0] = 1;
    oldLoc[0] = 1;

    for (uint8_t i = 0; i < rs->T; i++) {
        uint8_t delta = syndromes[i];
        for (uint8_t j = 1; j < errLocLen; j++) {
            delta = GfSub(delta, GfMul(errLoc[j], syndromes[i - j]));
        }

        for (uint8_t j = 0; j < oldLocLen; j++) {
            oldLoc[oldLocLen - j] = oldLoc[oldLocLen - j - 1];
        }
        oldLoc[0] = 0;

        oldLocLen++;

        if (delta != 0) {
            if (oldLocLen > errLocLen) {
                GfPolyScale(oldLoc, oldLocLen, delta, newLoc);
                newLocLen = oldLocLen;
                GfPolyScale(errLoc, errLocLen, GfInv(delta), oldLoc);
                oldLocLen = errLocLen;
                memcpy(errLoc, newLoc, newLocLen);
                errLocLen = newLocLen;
            }
            GfPolyScale(oldLoc, oldLocLen, delta, newLoc);
            newLocLen = oldLocLen;
            memcpy(tmpLoc, errLoc, errLocLen);
            errLocLen = GfPolyAdd(tmpLoc, errLocLen, newLoc, newLocLen, errLoc);
        }
    }

    uint8_t index = 0;
    for (uint8_t i = 0; i < errLocLen; i++) {
        if ((index == 0) && (errLoc[i] == 0)) // drop leading zeros
            continue;

        out[index++] = errLoc[i];
    }

    if (((errLocLen - 1) << 1) > rs->T)
        return false;

    *outSize = errLocLen;
    return true;
}

// @brief Calculates error magnitude (errata) polynomial and fix data
// @param *rs RS instance
// @param *data Input data block
// @param size Block size = N
// @param *syn Syndrome polynomial. Clobbered: it is reversed in place and then
//             reused as scratch for the error locator derivative.
// @param *evaluator Error evaluator polynomial
// @param errCount Number of errors (error evaulator size)
// @return True on success, false on failure
static bool fix(const struct LwFecRS *rs, uint8_t *data, uint8_t size, uint8_t *syn, const uint8_t *evaluator, uint8_t errCount) {
    // This is based on Forney's algorithm. The two working polynomials take
    // 3 * RS_MAX_REDUNDANCY_BYTES + 3 bytes of the common buffer between them:
    // RS_MAX_REDUNDANCY_BYTES + 1 for the locator and twice that for the
    // errata evaluator.
    uint8_t *locator = commonBuffer;
    uint8_t *errataEvaluator = locator + RS_MAX_REDUNDANCY_BYTES + 1;

    memset(locator, 0, rs->T + 1);
    memset(errataEvaluator, 0, 2 * rs->T + 2);

    locator[0] = 1; // initialize error locator to constant
    uint8_t locatorSize = 1;

    // use "errataEvaluator" as temporary variable
    for (uint8_t i = 0; i < errCount; i++) {
        memcpy(errataEvaluator, locator, rs->T);
        GfPolyMul(errataEvaluator, locatorSize, (uint8_t[2]){ GfPow(2, size - 1 - evaluator[i]), 1 }, 2, locator);
        locatorSize++;
    }

    memset(errataEvaluator, 0, 2 * rs->T + 2);

    GfPolyInv(syn, rs->T);
    GfPolyMul(syn, rs->T, locator, locatorSize, errataEvaluator);
    for (uint8_t i = 0; i < locatorSize; i++) {
        errataEvaluator[i] = errataEvaluator[rs->T + i];
    }

    uint8_t errataPosition[RS_MAX_REDUNDANCY_BYTES];
    for (uint8_t i = 0; i < errCount; i++) {
        errataPosition[i] = GfPow(2, size - 1 - evaluator[i]);
    }

    uint8_t *errLocPrimePoly = syn; // reuse
    uint8_t errLocPrimePolyLen = 0;
    for (uint8_t i = 0; i < errCount; i++) {
        errLocPrimePolyLen = 0;
        uint8_t errataInv = GfInv(errataPosition[i]);
        for (uint8_t j = 0; j < errCount; j++) {
            if (j != i) {
                errLocPrimePoly[errLocPrimePolyLen++] = GfSub(1, GfMul(errataInv, errataPosition[j]));
            }
        }
        uint8_t errLocPrime = 1;
        for (uint8_t j = 0; j < errLocPrimePolyLen; j++) {
            errLocPrime = GfMul(errLocPrime, errLocPrimePoly[j]);
        }

        uint8_t y = GfPolyEval(errataEvaluator, locatorSize, errataInv);
        // in general y*=errataInv**(fcr-1)
        // for fcr=0 y*=errataInv**-1=errataPosition
        // for fcr=1 y does not change
        if (rs->fcr == 0)
            y = GfMul(y, errataPosition[i]);
        else if (rs->fcr > 0)
            y = GfMul(y, GfPow(errataInv, rs->fcr - 1));

        if (errLocPrime == 0) {
            return false;
        }
        data[evaluator[i]] = GfSub(data[evaluator[i]], GfDiv(y, errLocPrime));
    }

    return true;
}

// @brief Check if syndromes are all zero, that is if the message is correct
// @param *syndromes Syndrome polynomial
// @param size Syndrome polynomial size (buffer length)
// @return True if all zero
static bool checkSyndromes(const uint8_t *syndromes, uint8_t size) {
    bool err = false;

    for (uint8_t i = 0; i < size; i++) // check if all syndromes are 0, if so, the message is correct
    {
        if (syndromes[i] != 0) {
            err = true;
        }
    }
    return !err;
}

bool RsDecode(const struct LwFecRS *rs, uint8_t *data, size_t dataCapacity, uint8_t size, uint8_t *fixed) {
    // The block is always processed at its natural length N: the parity bytes
    // are relocated to the tail and the gap is zero-filled, so every byte up to
    // data[RS_BLOCK_SIZE - 1] is written whatever "size" says. "dataCapacity"
    // is what the caller actually owns. The assert reports a buffer sized to
    // "size" instead of to N right where such a caller is introduced, and the
    // check below keeps builds with NDEBUG from writing out of bounds.
    assert(dataCapacity >= RS_BLOCK_SIZE);
    if (dataCapacity < RS_BLOCK_SIZE)
        return false;

    if ((size > (RS_BLOCK_SIZE - rs->T)) || (rs->T > RS_MAX_REDUNDANCY_BYTES))
        return false;

    // This function needs 3 arrays of RS_MAX_REDUNDANCY_BYTES + 1 each
    static uint8_t syn[RS_MAX_REDUNDANCY_BYTES + 1];
    static uint8_t locator[RS_MAX_REDUNDANCY_BYTES + 1];
    static uint8_t evaluator[RS_MAX_REDUNDANCY_BYTES + 1];

    memset(syn, 0, rs->T + 1);
    memset(locator, 0, rs->T + 1);
    memset(evaluator, 0, rs->T + 1);

    memmove(&data[RS_BLOCK_SIZE - rs->T], &data[size], rs->T);
    memset(&data[size], 0, RS_BLOCK_SIZE - size - rs->T);

    syndromes(rs, data, RS_BLOCK_SIZE, syn); // calculate syndromes

    if (checkSyndromes(syn, rs->T))
        return true;

    uint8_t locatorSize = 0;

    if (!errorLocator(rs, syn, locator, &locatorSize)) // calculate error locator polynomial
        return false;

    if (!errorEvaluator(locator, locatorSize, evaluator)) // calculate error evaulator (list of erroneous positions)
        return false;

    if (!fix(rs, data, RS_BLOCK_SIZE, syn, evaluator, locatorSize - 1)) // calculate error magnitude (errata) polynomial and try to fix
        return false;

    syndromes(rs, data, RS_BLOCK_SIZE, syn); // calculate syndromes again to check if the message has been corrected successfully

    if (checkSyndromes(syn, rs->T)) {
        *fixed = locatorSize - 1;
        return true;
    } else
        return false;
}

void RsEncode(const struct LwFecRS *rs, uint8_t *data, size_t dataCapacity, uint8_t size) {
    // Same whole-block contract as RsDecode(): the zero fill and the parity
    // write reach data[RS_BLOCK_SIZE - 1] regardless of "size", so a buffer
    // sized to "size" is rejected here rather than overflowed.
    assert(dataCapacity >= RS_BLOCK_SIZE);
    if (dataCapacity < RS_BLOCK_SIZE)
        return;

    if ((size > (RS_BLOCK_SIZE - rs->T)) || (rs->T > RS_MAX_REDUNDANCY_BYTES))
        return;

    memset(&data[size], 0, RS_BLOCK_SIZE - size);
    static uint8_t t[RS_BLOCK_SIZE];
    memset(t, 0, sizeof(t));
    memcpy(&data[size], GfPolyDiv(data, RS_BLOCK_SIZE, rs->generator, rs->T + 1, t), rs->T);
}

void RsInit(struct LwFecRS *rs, uint8_t T, uint8_t fcr) {
    if (T > RS_MAX_REDUNDANCY_BYTES)
        return;

    static uint8_t temp[RS_MAX_REDUNDANCY_BYTES + 1];
    memset(rs->generator, 0, T + 1);
    rs->generator[0] = 1;
    for (uint8_t i = 0; i < T; i++) {
        memcpy(temp, rs->generator, i + 1);
        GfPolyMul(temp, i + 1, (uint8_t[2]){ 1, GfPow2(i + fcr) }, 2, rs->generator);
    }
    rs->T = T;
    rs->fcr = fcr;
}

#if RS_BLOCK_SIZE > 256
#error Architectural limit of RS FEC block size is 256 bytes
#endif

#if RS_MAX_DATA_SIZE <= 0
#error RS FEC parity byte count must be less than the block size
#endif