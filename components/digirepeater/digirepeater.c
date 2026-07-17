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
                    strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
                    packet->rpt_list[idx].ssid = g_config.digi_ssid;
                    packet->rpt_flags |= (1 << idx);
                    s_stats.txPkts++;
                    return 2;
                }
            } else {
                idx = 0;
                strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
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
                    strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
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
                strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
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
                strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
                packet->rpt_list[idx].ssid = g_config.digi_ssid;
                packet->rpt_flags |= (1 << idx);
                j = 2;
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
                strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
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
            strcpy(packet->rpt_list[idx].call, g_config.digi_mycall);
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
