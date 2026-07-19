/**
 * @file digirepeater.c
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
 * @brief APRS digipeater path logic: WIDEn-N / TRACEn-N / RELAY / ECHO / GATE
 * handling, hop decrementing and callsign insertion, duplicate suppression and
 * per-station statistics kept across deep sleep.
 */

#include <string.h>

#include "esp_attr.h"

#include "app_config.h"
#include "digirepeater.h"

// Preserved across deep-sleep the same way the original firmware did.
static RTC_DATA_ATTR digi_stats_t s_stats;

digi_stats_t digi_get_stats(void) {
    return s_stats;
}

void digi_reset_stats(void) {
    memset(&s_stats, 0, sizeof(s_stats));
}

/* Bounded copy into an AX.25 callsign field (char[6 + CALL_OVERSPACE] == 7
 * bytes). AX.25 callsigns are at most 6 chars; anything longer is truncated so
 * a stale or hand-edited config.json can never overflow the 7-byte destination
 * (which would otherwise corrupt the adjacent ssid / next rpt_list entry). */
static inline void copy_call(char dst[7], const char *src) {
    size_t i = 0;
    for (; i < 6 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

int digiProcess(ax25_msg_t *packet) {
    int idx, j;
    uint8_t ctmp;

    s_stats.rxPkts++;

    if (packet->len < 5) {
        s_stats.erPkts++;
        return 0; // no destination / malformed
    }

    if (!strncmp(packet->src.call, "NOCALL", 6)) {
        s_stats.dropRx++;
        return 0;
    }
    if (!strncmp(packet->src.call, "MYCALL", 6)) {
        s_stats.dropRx++;
        return 0;
    }

    // Destination SSID trace (WIDEn-N encoded in the dest SSID field)
    if (packet->dst.ssid > 0) {
        ctmp = packet->dst.ssid & 0x1E;

        if (ctmp > 15)
            ctmp = 0;

        if (ctmp < 8) {
            if (ctmp > 0)
                ctmp--;
            packet->dst.ssid = ctmp;

            if (packet->rpt_count > 0) {
                for (idx = 0; idx < packet->rpt_count; idx++) {
                    if (!strcmp(packet->rpt_list[idx].call, g_config.digi_mycall)) {
                        if (packet->rpt_list[idx].ssid == g_config.digi_ssid) {
                            if (packet->rpt_flags & (1 << idx)) {
                                s_stats.dropRx++;
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
                        s_stats.dropRx++;
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
                    copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                    packet->rpt_list[idx].ssid = g_config.digi_ssid;
                    packet->rpt_flags |= (1 << idx);
                    s_stats.txPkts++;
                    return 2;
                }
            } else {
                idx = 0;
                copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                packet->rpt_list[idx].ssid = g_config.digi_ssid;
                packet->rpt_flags |= (1 << idx);
                packet->rpt_count += 1;
                s_stats.txPkts++;
                return 2;
            }
        } else {
            s_stats.dropRx++;
            return 0; // no usable path
        }
    }

    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "qA", 2)) {
            s_stats.dropRx++;
            return 0;
        }
    }
    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (!strncmp(packet->rpt_list[idx].call, "TCP", 3)) {
            s_stats.dropRx++;
            return 0;
        }
    }

    j = 0;
    for (idx = 0; idx < packet->rpt_count; idx++) {
        if (packet->rpt_flags & (1 << idx)) {
            continue; // already relayed, bypass
        }

        if (!strncmp(packet->rpt_list[idx].call, "WIDE", 4)) {
            if (packet->rpt_list[idx].ssid > 0) {
                ctmp = packet->rpt_list[idx].ssid & 0x1F;
                if (ctmp > 0)
                    ctmp--;
                if (ctmp > 15)
                    ctmp = 0;
                if (ctmp == 0) {
                    copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                    packet->rpt_list[idx].ssid = g_config.digi_ssid;
                    packet->rpt_flags |= (1 << idx);
                    j = 2;
                    break;
                } else {
                    packet->rpt_list[idx].ssid = ctmp;
                    packet->rpt_flags &= ~(1 << idx);
                    j = 2;
                    break;
                }
            } else {
                copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                packet->rpt_list[idx].ssid = g_config.digi_ssid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
        } else if (!strncmp(packet->rpt_list[idx].call, "TRACE", 5)) {
            ctmp = packet->rpt_list[idx].ssid & 0x1F;
            if (ctmp > 0)
                ctmp--;
            if (ctmp > 15)
                ctmp = 0;
            if (ctmp == 0) {
                copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                packet->rpt_list[idx].ssid = g_config.digi_ssid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
                break;
            } else if (packet->rpt_count >= AX25_MAX_RPT) {
                // Path already has AX25_MAX_RPT (8) digipeater addresses -
                // the maximum an AX.25 frame can carry. Inserting our own
                // call here would push rpt_count to 9, one past the end of
                // rpt_list[AX25_MAX_RPT], and the corresponding bit in the
                // uint8_t rpt_flags bitmask (1 << 8) would silently be lost.
                // Drop instead of corrupting the path, same as any other
                // "no usable path" case below.
                s_stats.dropRx++;
                j = 0;
                break;
            } else {
                int n;
                for (j = idx; j < packet->rpt_count; j++) {
                    if (packet->rpt_flags & (1 << j))
                        break;
                }
                for (; j >= idx; j--) {
                    n = j + 1;
                    if (n >= AX25_MAX_RPT)
                        break;
                    strcpy(packet->rpt_list[n].call, packet->rpt_list[j].call);
                    packet->rpt_list[n].ssid = packet->rpt_list[j].ssid;
                    if (packet->rpt_flags & (1 << j))
                        packet->rpt_flags |= (1 << n);
                    else
                        packet->rpt_flags &= ~(1 << n);
                }
                if (idx + 1 < AX25_MAX_RPT)
                    packet->rpt_list[idx + 1].ssid = ctmp;

                packet->rpt_count += 1;
                copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
                packet->rpt_list[idx].ssid = g_config.digi_ssid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
        } else if (!strncmp(packet->rpt_list[idx].call, "RFONLY", 6)) {
            packet->rpt_flags |= (1 << idx);
            j = 2;
            break;
        } else if (!strncmp(packet->rpt_list[idx].call, "RELAY", 5) || !strncmp(packet->rpt_list[idx].call, "GATE", 4) ||
                   !strncmp(packet->rpt_list[idx].call, "ECHO", 4)) {
            copy_call(packet->rpt_list[idx].call, g_config.digi_mycall);
            packet->rpt_list[idx].ssid = g_config.digi_ssid;
            packet->rpt_flags |= (1 << idx);
            j = 2;
            break;
        } else if (!strcmp(packet->rpt_list[idx].call, g_config.digi_mycall)) {
            ctmp = packet->rpt_list[idx].ssid & 0x1F;
            if (ctmp == g_config.digi_ssid) {
                if (packet->rpt_flags & (1 << idx)) {
                    s_stats.dropRx++;
                    j = 0;
                    break;
                }
                packet->rpt_flags |= (1 << idx);
                j = 1;
                break;
            } else {
                j = 0;
                break;
            }
        } else {
            j = 0;
            break;
        }
    }

    if (j == 2)
        s_stats.txPkts++;

    return j;
}
