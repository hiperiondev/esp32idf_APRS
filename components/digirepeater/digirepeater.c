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
// the operator's alias table, hop-count trapping, callsign insertion,
// preemptive scanning of explicit routes and duplicate suppression.

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
        copy_call(packet->rpt_list[n].call, packet->rpt_list[j].call);
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

// Bits of the raw AX.25 address SSID octet touched when preemptive digipeating
// rewrites a path address: bit 7 is the has-been-repeated flag, bit 5 is the
// low reserved bit the preemptive proposal sets on every address the scan
// skipped, and bits 4:1 carry the SSID. Bit 6 (the high reserved bit), bit 0
// (the address-extension bit) and the command/response bit of the first two
// addresses are left exactly as received.
#define AX25_SSID_OCTET_H      0x80
#define AX25_SSID_OCTET_RR_LOW 0x20
#define AX25_SSID_OCTET_KEEP   0x41

// Rewrite one path address' raw SSID octet so it agrees with the decoded
// callsign/SSID pair and carries the repeated flag.
//
// The frame this station puts back on the air is re-encoded from its TNC2
// rendering, which expresses an address as callsign, SSID and the used marker
// alone, so the reserved bits do not reach the channel. Maintaining the octet
// still matters: it is the raw form every other consumer of the decoded frame
// reads, and leaving it describing the address that used to be in this slot
// would make the two views of the same frame disagree.
//
// The other proposals for these two bits are absent in both directions: the
// reserved pair is neither read as the sender's routing precedence nor
// written to advertise that an operator is present at this station. They ride
// in the same octet, so they would meet the same TNC2 re-encoding on the way
// out, and a signal a receiver only sometimes gets is worse than none.
static inline void set_ssid_octet(ax25_call_t *field, uint8_t ssid, bool rr_low) {
    field->ssidBits =
        (uint8_t)((field->ssidBits & AX25_SSID_OCTET_KEEP) | AX25_SSID_OCTET_H | (rr_low ? AX25_SSID_OCTET_RR_LOW : 0) | (uint8_t)((ssid & 0x0F) << 1));
}

// May a preemptive scan claim this alias row? Only a fixed alias - a name that
// stands for this station and nothing else - qualifies. A row holding
// DIGI_ALIAS_WILDCARD, or one whose name ends in a decimal digit, is the
// generic XXXXn-N form, which the preemptive proposal excludes explicitly:
// jumping ahead to a WIDEn or TRACEn further down the path would repeat a
// flooding request the network expects to travel one hop at a time.
static bool alias_is_preemptable(const digi_alias_t *row) {
    size_t n;

    if (row->mode == DIGI_ALIAS_OFF)
        return false;

    n = strlen(row->alias);
    if (n == 0)
        return false;

    for (size_t i = 0; i < n; i++) {
        if (row->alias[i] == DIGI_ALIAS_WILDCARD)
            return false;
    }

    return row->alias[n - 1] < '0' || row->alias[n - 1] > '9';
}

// Does this path address name the station itself? Either the digipeater's own
// callsign and SSID, or one of the fixed alias rows above. An alias is claimed
// only when the address carries SSID 0: an alias written with a hop count is a
// routing request in the n-N sense, and answering it out of turn is the very
// thing the proposal's generic exclusion forbids.
static bool preempt_matches(const ax25_call_t *field, const digi_alias_t *table, const char *myCall, uint8_t mySsid) {
    if (!strcmp(field->call, myCall) && field->ssid == mySsid)
        return true;

    if (field->ssid != 0)
        return false;

    for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
        if (alias_is_preemptable(&table[i]) && alias_matches(table[i].alias, field->call))
            return true;
    }

    return false;
}

// Preemptive digipeating: scan from the first unused path address to the end
// of the path for one of this station's own identities and, on a match, repeat
// the frame now rather than waiting for the addresses in front of it to be
// served by the digipeaters they name.
//
// This is what carries an explicit route such as WIDE1-1,CITYA,WIDE2-1,CITYB:
// every station listed in it repeats the frame when its turn comes into view,
// the addresses still ahead stay live, and the channel carries one copy per
// named hop instead of the exponential fan-out of a WIDEn-N flood.
//
// A match at the first unused address is not preemption - nothing is being
// jumped over - so it is left to the ordinary path logic, which is what keeps
// an explicit first hop and a generic n-N request behaving exactly as they do
// with the scan switched off.
//
// Returns 2 when the frame was claimed and its path rewritten, 0 when the scan
// found nothing to do and the caller should carry on.
static int digipeat_preemptive(ax25_msg_t *packet, const digi_alias_t *table, const char *myCall, uint8_t mySsid, uint8_t mode) {
    int first = -1;
    int match = -1;

    for (int idx = 0; idx < packet->rpt_count; idx++) {
        if (AX25_REPEATED(packet, idx))
            continue;
        if (first < 0)
            first = idx;
        if (preempt_matches(&packet->rpt_list[idx], table, myCall, mySsid)) {
            match = idx;
            break;
        }
    }

    if (match < 0 || match <= first)
        return 0;

    if (mode == DIGI_PREEMPT_MARK) {
        // Every address from the first unused one up to and including the
        // match is marked used, so the path still records the route the
        // originating station asked for and the addresses behind the match
        // cannot be served a second time by anybody else.
        for (int idx = first; idx <= match; idx++) {
            packet->rpt_flags |= (uint8_t)(1u << idx);
            set_ssid_octet(&packet->rpt_list[idx], packet->rpt_list[idx].ssid, true);
        }
    } else {
        // The addresses in front of the match are discarded and the match
        // becomes the head of the path, which leaves the frame carrying only
        // what is still to be done.
        uint8_t flags = 0;
        int kept = packet->rpt_count - match;

        for (int idx = 0; idx < kept; idx++) {
            packet->rpt_list[idx] = packet->rpt_list[match + idx];
            if (packet->rpt_flags & (1u << (match + idx)))
                flags |= (uint8_t)(1u << idx);
        }
        packet->rpt_count = (uint8_t)kept;
        packet->rpt_flags = flags;
        match = 0;
        packet->rpt_flags |= 1u;
    }

    // In both modes the matched alias becomes this station's callsign, marked
    // used, so the hop is attributable afterwards exactly as a traced n-N hop
    // is. Whatever follows it in the path is untouched and stays live.
    copy_call(packet->rpt_list[match].call, myCall);
    packet->rpt_list[match].ssid = mySsid;
    set_ssid_octet(&packet->rpt_list[match], mySsid, mode == DIGI_PREEMPT_MARK);

    return 2;
}

// Pre-New-N routing on the AX.25 destination SSID, where a hop count of 1 to 7
// in the destination address stands in for a WIDEn-N path entry. A frame that
// carries one is repeated on the strength of that SSID alone: the count is
// decremented and this station's callsign goes into the path marked used, so
// the hop can still be attributed afterwards. A destination SSID of 8 to 15
// belongs to the destination address itself and means nothing here.
//
// Returns the caller's own routing code - 1 when an entry naming this station
// was simply marked used, 2 when the callsign was inserted - or 0 when this
// convention does not route the frame, in which case the frame is left exactly
// as it arrived and the operator's alias table decides what happens to it.
// That is why the decremented destination SSID is written back only on the two
// paths that do repeat: a frame handed on to the alias table must still carry
// the hop count it was received with.
static int digipeat_by_dest_ssid(ax25_msg_t *packet, const char *myCall, uint8_t mySsid) {
    uint8_t hops = packet->dst.ssid;

    if (hops == 0 || hops >= 8)
        return 0;

    if (packet->rpt_count == 0) {
        // Nothing to shift and no entry to scan, so the callsign is written
        // straight into the first slot rather than through insert_own_call(),
        // whose scan starts by reading the entry it is asked to displace.
        packet->dst.ssid = hops - 1;
        copy_call(packet->rpt_list[0].call, myCall);
        packet->rpt_list[0].ssid = mySsid;
        packet->rpt_flags |= (1 << 0);
        packet->rpt_count += 1;
        return 2;
    }

    for (int idx = 0; idx < packet->rpt_count; idx++) {
        if (!strcmp(packet->rpt_list[idx].call, myCall) && packet->rpt_list[idx].ssid == mySsid) {
            if (packet->rpt_flags & (1 << idx))
                return 0;
            packet->dst.ssid = hops - 1;
            packet->rpt_flags |= (1 << idx);
            return 1;
        }
        if (packet->rpt_flags & (1 << idx))
            continue;

        // Inserting here would push rpt_count past AX25_MAX_RPT (8), one entry
        // beyond what rpt_list holds and beyond the last bit of the uint8_t
        // rpt_flags bitmask.
        if (packet->rpt_count >= AX25_MAX_RPT)
            return 0;

        packet->dst.ssid = hops - 1;
        insert_own_call(packet, idx, myCall, mySsid);
        return 2;
    }

    return 0;
}

int digiProcess(ax25_msg_t *packet) {
    int idx, j;

    // Snapshot the digipeater's own call/SSID once at entry. This runs on the
    // modem RX task and compares/copies digi_mycall many times below; a web
    // save rewriting it mid-run could otherwise cause a torn compare or a
    // copy of an unterminated callsign into the outgoing path.
    char digiMyCall[10];
    uint8_t digiMySsid;
    digi_alias_t digiAlias[DIGI_ALIAS_MAX];
    bool digiFillinOnly;
    bool digiTrapClamp;
    bool digiDestSsidEn;
    uint8_t digiPreempt;
    app_config_lock();
    memcpy(digiMyCall, g_config.digi_mycall, sizeof(digiMyCall));
    digiMySsid = g_config.digi_ssid;
    memcpy(digiAlias, g_config.digi_alias, sizeof(digiAlias));
    digiFillinOnly = g_config.digi_fillin_only;
    digiTrapClamp = g_config.digi_trap_n_clamp;
    digiDestSsidEn = g_config.digi_dest_ssid_en;
    digiPreempt = g_config.digi_preempt;
    app_config_unlock();

    if (packet->len == 0) {
        igate_note_drop(DROP_DIGI_MALFORMED);
        return 0; // empty info field: nothing to repeat
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

    // Routing on the destination SSID is off unless the operator turns it on:
    // it repeats a frame ahead of the alias table and on the strength of a
    // single SSID nibble, so the explicit path an originating station wrote
    // would never be consulted. When it declines the frame, or when it is
    // switched off, the alias table below is what decides.
    if (digiDestSsidEn) {
        int routed = digipeat_by_dest_ssid(packet, digiMyCall, digiMySsid);
        if (routed != 0)
            return routed;
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

    // Preemptive digipeating runs once the explicit first-hop and self checks
    // above have had their say and before the alias table decides anything, so
    // an explicit route that names this station further down is served, while
    // a generic n-N request - which the scan never claims - still reaches the
    // table untouched.
    if (digiPreempt != DIGI_PREEMPT_OFF) {
        int routed = digipeat_preemptive(packet, digiAlias, digiMyCall, digiMySsid, digiPreempt);
        if (routed != 0)
            return routed;
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

        if (!strncmp(packet->rpt_list[idx].call, "RFONLY", 6) || !strncmp(packet->rpt_list[idx].call, "NOGATE", 6)) {
            // Neither is a digipeat alias - APRS 1.1 defines both purely as
            // IGate-suppression tokens (igate.c is what acts on them, on the
            // RF->INET leg). Left as-is here and skipped without being marked
            // used, so digipeating falls through to whatever routing
            // instruction the path carries next, exactly as if this token
            // were not present at all.
            continue;
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
