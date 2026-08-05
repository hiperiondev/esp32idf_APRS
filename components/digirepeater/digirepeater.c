// @file digirepeater.c
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
// @brief APRS digipeater path logic: New n-N Paradigm alias handling driven by
// the operator's alias table, hop-count trapping, callsign insertion and
// duplicate suppression.

#include <ctype.h>
#include <string.h>

#include "app_config.h"
#include "digirepeater.h"
#include "igate.h"

// Every frame this file discards is reported through igate_note_drop(), whose
// per-reason table is what the web dashboard's Drop Breakdown renders, and the
// headline rx/tx/digi figures are counted in aprs_service.c at the points where
// frames actually flow. Both sets move regardless of whether digi_en/igate_en
// are on, so the digipeater keeps no counters of its own.

// Bounded copy into an AX.25 callsign field (char[6 + CALL_OVERSPACE] == 7
// bytes). AX.25 callsigns are at most 6 chars; anything longer is truncated so
// a stale or hand-edited config.json can never overflow the 7-byte destination
// (which would otherwise corrupt the adjacent ssid / next rpt_list entry).
static inline void copy_call(char dst[7], const char *src) {
    size_t i = 0;
    for (; i < 6 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

// Does one received repeater callsign match one alias row? The comparison is
// case-insensitive and requires equal length, and DIGI_ALIAS_WILDCARD in the
// row matches exactly one decimal digit. Equal length is what keeps "WIDE#"
// from claiming a station calling itself WIDEN or WIDE12, and what keeps a row
// of "WIDE1" from matching anything but that alias.
static bool alias_matches(const char *row, const char *call) {
    size_t n = strlen(row);
    if (n == 0 || n != strlen(call))
        return false;

    for (size_t i = 0; i < n; i++) {
        if (row[i] == DIGI_ALIAS_WILDCARD) {
            if (call[i] < '0' || call[i] > '9')
                return false;
        } else if (toupper((unsigned char)row[i]) != toupper((unsigned char)call[i])) {
            return false;
        }
    }
    return true;
}

// First alias row that claims this callsign, or NULL if none does. Rows are
// consulted in table order and the first hit wins, so a specific alias placed
// above a wildcard row keeps its own hop limit.
//
// In fill-in mode only single-hop rows are considered. That is the whole of
// the fill-in role: the station lifts traffic from neighbours who cannot reach
// the backbone directly and leaves everything routed for more hops to the wide
// digipeaters, which is what stops a home station in a valley from adding a
// redundant copy of every packet in the region.
static const digi_alias_t *find_alias(const digi_alias_t *table, const char *call, bool fillin_only) {
    for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
        if (table[i].mode == DIGI_ALIAS_OFF)
            continue;
        if (fillin_only && table[i].max_n != 1)
            continue;
        if (alias_matches(table[i].alias, call))
            return &table[i];
    }
    return NULL;
}

// Insert this station's callsign at rpt_list[idx], marked used, pushing the
// entry that was there (and every unused entry after it) one slot down. The
// caller has already made room by checking rpt_count against AX25_MAX_RPT.
static void insert_own_call(ax25_msg_t *packet, int idx, const char *myCall, uint8_t mySsid) {
    int j;
    for (j = idx; j < packet->rpt_count; j++) {
        if (packet->rpt_flags & (1 << j))
            break;
    }
    for (; j >= idx; j--) {
        int n = j + 1;
        if (n >= AX25_MAX_RPT)
            break;
        strcpy(packet->rpt_list[n].call, packet->rpt_list[j].call);
        packet->rpt_list[n].ssid = packet->rpt_list[j].ssid;
        if (packet->rpt_flags & (1 << j))
            packet->rpt_flags |= (1 << n);
        else
            packet->rpt_flags &= ~(1 << n);
    }

    packet->rpt_count += 1;
    copy_call(packet->rpt_list[idx].call, myCall);
    packet->rpt_list[idx].ssid = mySsid;
    packet->rpt_flags |= (1 << idx);
}

int digiProcess(ax25_msg_t *packet) {
    int idx, j;
    uint8_t ctmp;

    // Snapshot the digipeater's own call/SSID once at entry. This runs on the
    // modem RX task and compares/copies digi_mycall many times below; a web
    // save rewriting it mid-run could otherwise cause a torn compare or a
    // copy of an unterminated callsign into the outgoing path.
    char digiMyCall[10];
    uint8_t digiMySsid;
    digi_alias_t digiAlias[DIGI_ALIAS_MAX];
    bool digiFillinOnly;
    bool digiTrapClamp;
    app_config_lock();
    memcpy(digiMyCall, g_config.digi_mycall, sizeof(digiMyCall));
    digiMySsid = g_config.digi_ssid;
    memcpy(digiAlias, g_config.digi_alias, sizeof(digiAlias));
    digiFillinOnly = g_config.digi_fillin_only;
    digiTrapClamp = g_config.digi_trap_n_clamp;
    app_config_unlock();

    if (packet->len < 5) {
        igate_note_drop(DROP_DIGI_MALFORMED);
        return 0; // no destination / malformed
    }

    if (!strncmp(packet->src.call, "NOCALL", 6)) {
        igate_note_drop(DROP_DIGI_PLACEHOLDER_CALL);
        return 0;
    }
    if (!strncmp(packet->src.call, "MYCALL", 6)) {
        igate_note_drop(DROP_DIGI_PLACEHOLDER_CALL);
        return 0;
    }

    // Duplicate suppression (APRS101: a digipeater ignores a frame it has
    // already seen within roughly the last 30 seconds). The window is keyed on
    // the source address and the information field only - never on the path -
    // so every copy of one transmission looks the same here no matter which
    // route it arrived by. That is what stops a pair of digipeaters inside
    // each other's coverage from bouncing the same frame back and forth, and
    // what absorbs an RF echo of a frame this station has just repeated
    // itself.
    //
    // The test runs before any path work so that a duplicate costs nothing and
    // leaves the caller's frame untouched. DUP_SCOPE_DIGI keeps this window
    // separate from the IGate's own RF->INET window, which sees the very same
    // frames from the same RX dispatch.
    if (isDuplicatePacketScoped(packet, DUP_SCOPE_DIGI)) {
        igate_note_drop(DROP_DIGI_DUPLICATE);
        return 0;
    }

    // Destination SSID trace (WIDEn-N encoded in the dest SSID field).
    //
    // ax25_decode() already shifts the raw address octet down into a plain
    // 0-15 hop count ((ssidBits >> 1) & 0x0F) and stores it in dst.ssid, so it
    // is used here as-is; the range checks below then operate on that decoded
    // domain and the decremented value is written back in the same form,
    // keeping the retransmitted frame consistent with what was received.
    if (packet->dst.ssid > 0) {
        ctmp = packet->dst.ssid;

        if (ctmp > 15)
            ctmp = 0;

        if (ctmp < 8) {
            if (ctmp > 0)
                ctmp--;
            packet->dst.ssid = ctmp;

            if (packet->rpt_count > 0) {
                for (idx = 0; idx < packet->rpt_count; idx++) {
                    if (!strcmp(packet->rpt_list[idx].call, digiMyCall)) {
                        if (packet->rpt_list[idx].ssid == digiMySsid) {
                            if (packet->rpt_flags & (1 << idx)) {
                                igate_note_drop(DROP_DIGI_ALREADY_USED);
                                return 0; // already used *
                            }
                            packet->rpt_flags |= (1 << idx);
                            return 1;
                        }
                    }
                    if (packet->rpt_flags & (1 << idx))
                        continue;

                    // The path is already at the AX.25 maximum of AX25_MAX_RPT
                    // (8) digipeater addresses, so inserting our own call here
                    // would push rpt_count to 9 - one past the end of
                    // rpt_list[AX25_MAX_RPT] - and, since rpt_flags is a
                    // uint8_t (one bit per entry, 8 bits total), the
                    // "already repeated" bit for that 9th slot would silently
                    // be lost (1 << 8 truncates to 0). Treat a full path the
                    // same as "no usable path" instead of corrupting it.
                    if (packet->rpt_count >= AX25_MAX_RPT) {
                        igate_note_drop(DROP_DIGI_PATH_FULL);
                        return 0;
                    }

                    for (j = idx; j < packet->rpt_count; j++) {
                        if (packet->rpt_flags & (1 << j))
                            break;
                    }
                    for (; j >= idx; j--) {
                        int n = j + 1;
                        if (n >= AX25_MAX_RPT)
                            break;
                        strcpy(packet->rpt_list[n].call, packet->rpt_list[j].call);
                        packet->rpt_list[n].ssid = packet->rpt_list[j].ssid;
                        if (packet->rpt_flags & (1 << j))
                            packet->rpt_flags |= (1 << n);
                        else
                            packet->rpt_flags &= ~(1 << n);
                    }

                    packet->rpt_count += 1;
                    copy_call(packet->rpt_list[idx].call, digiMyCall);
                    packet->rpt_list[idx].ssid = digiMySsid;
                    packet->rpt_flags |= (1 << idx);
                    return 2;
                }
            } else {
                idx = 0;
                copy_call(packet->rpt_list[idx].call, digiMyCall);
                packet->rpt_list[idx].ssid = digiMySsid;
                packet->rpt_flags |= (1 << idx);
                packet->rpt_count += 1;
                return 2;
            }
        } else {
            igate_note_drop(DROP_DIGI_NO_PATH);
            return 0; // no usable path
        }
    }

    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "qA", 2)) {
            igate_note_drop(DROP_DIGI_PATH_TOKEN);
            return 0;
        }
    }
    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "TCP", 3)) {
            igate_note_drop(DROP_DIGI_PATH_TOKEN);
            return 0;
        }
    }

    // A frame that already carries this station's callsign marked used has
    // been through here before. Repeating it again would put a second copy of
    // the same transmission on the air however many hops its path still has
    // left, so it stops here regardless of what follows.
    for (idx = 0; idx < packet->rpt_count; idx++) {
        if ((packet->rpt_flags & (1 << idx)) && !strcmp(packet->rpt_list[idx].call, digiMyCall) && packet->rpt_list[idx].ssid == digiMySsid) {
            igate_note_drop(DROP_DIGI_ALREADY_USED);
            return 0;
        }
    }

    j = 0;
    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (packet->rpt_flags & (1 << idx)) {
            continue; // already relayed, bypass
        }

        // ax25_decode() hands over the hop count already shifted out of the
        // raw address octet, so the SSID field reads as a plain 0-15 here and
        // the decremented value is written back in the same form.
        uint8_t hops = packet->rpt_list[idx].ssid;
        const digi_alias_t *alias = find_alias(digiAlias, packet->rpt_list[idx].call, digiFillinOnly);

        if (alias != NULL) {
            if (hops == 0) {
                // An alias with no hops left is spent: it is replaced by this
                // station's callsign, marked used, which both consumes the
                // entry and records who made the final hop.
                copy_call(packet->rpt_list[idx].call, digiMyCall);
                packet->rpt_list[idx].ssid = digiMySsid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
                break;
            }

            if (hops > alias->max_n) {
                // Trapping: a hop count above what the operator allows for
                // this alias is either brought down to the limit or refused
                // outright. Clamping keeps the frame moving while still
                // stopping it from flooding further than local conditions
                // allow, which is why it is the default; every extra hop
                // multiplies the load a frame puts on the network.
                if (!digiTrapClamp) {
                    igate_note_drop(DROP_DIGI_N_TRAPPED);
                    j = 0;
                    break;
                }
                hops = alias->max_n;
            }

            hops--;

            if (hops == 0) {
                copy_call(packet->rpt_list[idx].call, digiMyCall);
                packet->rpt_list[idx].ssid = digiMySsid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
                break;
            }

            if (alias->mode == DIGI_ALIAS_FLOOD) {
                // Flooding leaves the alias in place with one hop taken off
                // and no record of who took it.
                packet->rpt_list[idx].ssid = hops;
                packet->rpt_flags &= ~(1 << idx);
                j = 2;
                break;
            }

            // Tracing: this station's callsign goes in ahead of the remaining
            // alias and is marked used, so every hop of the path can be
            // attributed afterwards. This is what n-N routing is required to
            // do - it is the whole reason the paradigm moved WIDEn-N onto the
            // tracing mechanism.
            if (packet->rpt_count >= AX25_MAX_RPT) {
                // Path already has AX25_MAX_RPT (8) digipeater addresses -
                // the maximum an AX.25 frame can carry. Inserting our own
                // call here would push rpt_count to 9, one past the end of
                // rpt_list[AX25_MAX_RPT], and the corresponding bit in the
                // uint8_t rpt_flags bitmask (1 << 8) would silently be lost.
                // Drop instead of corrupting the path.
                igate_note_drop(DROP_DIGI_PATH_FULL);
                j = 0;
                break;
            }

            insert_own_call(packet, idx, digiMyCall, digiMySsid);
            if (idx + 1 < AX25_MAX_RPT)
                packet->rpt_list[idx + 1].ssid = hops;
            j = 2;
            break;
        }

        if (!strncmp(packet->rpt_list[idx].call, "RFONLY", 6)) {
            // Not an alias: a routing token that forbids the frame reaching
            // the Internet. It is consumed here so the rest of the path can
            // still be followed on RF.
            packet->rpt_flags |= (1 << idx);
            j = 2;
            break;
        }

        if (!strcmp(packet->rpt_list[idx].call, digiMyCall)) {
            if (packet->rpt_list[idx].ssid == digiMySsid) {
                packet->rpt_flags |= (1 << idx);
                j = 1;
                break;
            }
            j = 0;
            break;
        }

        // The next unused entry names neither an alias this station honours
        // nor this station itself, so the frame is addressed onward to
        // somebody else.
        j = 0;
        break;
    }

    return j;
}
