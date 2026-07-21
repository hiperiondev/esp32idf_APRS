/**
 * @file beacon_scheduler.h
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
 * @brief Single shared scheduler for all periodic "own-station transmit" work.
 *
 * The tracker, IGate and digipeater position beacons, the APRS weather report,
 * and the APRS bulletins used to each run in their own FreeRTOS task. Every one
 * of those tasks did the same thing - sleep, wake, build a packet, walk the
 * shared (float-heavy) TNC2/AX.25 TX chain, sleep again - and therefore each
 * had to carry a large stack (10-14 KB) sized for that call tree, even though
 * they almost never run at the same time and the half-duplex modem serialises
 * their transmissions anyway.
 *
 * This scheduler collapses those five tasks into one. On each pass it calls
 * every subsystem's "service" function (::beacon_service, ::weather_beacon_service,
 * ::bulletins_service), each of which transmits whatever is due and reports how
 * many seconds until it next needs servicing; the scheduler then sleeps until
 * the soonest of them. The subsystems keep their independent enable flags and
 * intervals - only the task (and its stack) is shared.
 *
 * Net effect: five stacks (~61 KB total) become one (~14 KB), freeing ~46 KB of
 * internal heap on this no-PSRAM build.
 */

#ifndef BEACON_SCHEDULER_H
#define BEACON_SCHEDULER_H

/**
 * @brief Start the single shared beacon/bulletin/weather scheduler task.
 *
 * Call once from application start-up, AFTER beacon_start(), weather_start()
 * and (if built) bulletins_start() have run, since those set up the state the
 * service functions read. Safe to call once.
 */
void beacon_scheduler_start(void);

#endif // BEACON_SCHEDULER_H
