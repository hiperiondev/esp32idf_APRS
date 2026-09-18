// @file heap_monitor.c
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
// @brief Periodic heap and stack sampling, heap brackets and an optional
// integrity sweep, driven from the APRS service's shared 1 Hz tick. See
// heap_monitor.h for what the figures mean and why they are sampled on the
// healthy path.

#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "heap_monitor.h"

#if CONFIG_APRS_HEAP_REPORT_TASKS
#include "esp_heap_task_info.h"
#endif

// Every reporting half is a compile-time option, and the bracket is reached
// through a macro that vanishes with it, so the tag only exists when at least
// one of them can log. The lock at the bottom of this file needs no tag of its
// own: it never logs anything, since a caller finding it held is an ordinary,
// silent outcome for that caller to act on, not a fault worth a log line here.
#if CONFIG_APRS_HEAP_REPORT || CONFIG_APRS_STACK_REPORT || CONFIG_APRS_HEAP_INTEGRITY_CHECK || CONFIG_APRS_HEAP_BRACKET
static const char *TAG = "heap_monitor";
#endif

#if CONFIG_APRS_HEAP_REPORT
static uint32_t s_report_elapsed_s;
#endif

#if CONFIG_APRS_STACK_REPORT
static uint32_t s_stack_elapsed_s;
#endif

#if CONFIG_APRS_HEAP_INTEGRITY_CHECK
static uint32_t s_integrity_elapsed_s;
#endif

void heap_monitor_bracket(const char *phase, const char *what) {
#if CONFIG_APRS_HEAP_BRACKET
    ESP_LOGI(TAG, "%s heap %s: %u bytes free with a %u byte largest block", what, phase, (unsigned)heap_caps_get_free_size(HEAP_MONITOR_CAPS),
             (unsigned)heap_caps_get_largest_free_block(HEAP_MONITOR_CAPS));
#else
    // Reachable only through a direct call: HEAP_MONITOR_BRACKET() expands to
    // nothing when the option is off, so nothing in the firmware gets here.
    (void)phase;
    (void)what;
#endif
}

#if CONFIG_APRS_STACK_REPORT
// Upper bound on how many tasks one report can describe. uxTaskGetSystemState()
// fills a caller-provided array and refuses outright rather than truncating if
// the array is too small, so this has to stay above the number of tasks alive
// at once: this firmware's own eight resident tasks, the two idle tasks, the
// timer service, and the handful the IDF runs for WiFi, lwIP, events and
// inter-processor calls - a little over twenty in the configuration that ships.
#define APRS_STACK_REPORT_MAX_TASKS 28

// Filled afresh on each report and never read between them. Static rather than
// automatic because it is a kilobyte of TaskStatus_t: putting it on the stack
// of the tick task would mean sizing that stack for a diagnostic that runs once
// an hour, and putting it on the heap would mean a report about memory pressure
// that fails under memory pressure.
static TaskStatus_t s_task_status[APRS_STACK_REPORT_MAX_TASKS];

static uint32_t s_stack_elapsed_s;

// One line per task with the least stack headroom it has had since it started.
// The mark only ever falls, so each line reports the worst case so far rather
// than a sample of this instant, and nothing is lost between lines.
//
// Every task is reported, not just this firmware's own: the IDF's WiFi, lwIP
// and event tasks have stacks of their own that come out of the same memory,
// and a budget that ignores them is not a budget.
//
// uxTaskGetSystemState() takes the kernel lock and fills the array in one pass,
// so each name arrives with its own high-water mark already beside it. Looking
// the tasks up one at a time and then querying each handle would leave a window
// between the two calls in which a task can be deleted - telegram_deinit() does
// exactly that to the Telegram poller - and the second call would then read a
// freed control block.
//
// The mark counts StackType_t units, which is a byte on Xtensa; the
// multiplication makes the figure bytes on any port and matches the way every
// other stack line in this firmware is spelled.
static void report_task_stacks(void) {
    UBaseType_t count = uxTaskGetSystemState(s_task_status, APRS_STACK_REPORT_MAX_TASKS, NULL);
    if (count == 0) {
        // The only way this fails: more tasks are alive than the array holds.
        // Reported rather than passed over, because a silent gap here reads
        // exactly like a period in which no task moved.
        ESP_LOGW(TAG, "stack report skipped, more than %d tasks alive", APRS_STACK_REPORT_MAX_TASKS);
        return;
    }
    for (UBaseType_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "stack %s: %u bytes free at its worst", s_task_status[i].pcTaskName,
                 (unsigned)(s_task_status[i].usStackHighWaterMark * sizeof(StackType_t)));
    }
}
#endif

void heap_monitor_tick_1hz(void) {
#if CONFIG_APRS_HEAP_REPORT
    if (++s_report_elapsed_s >= CONFIG_APRS_HEAP_REPORT_PERIOD_S) {
        s_report_elapsed_s = 0;

        // Three numbers on one line: what exists, what the largest single
        // allocation can still be, and how low the heap has ever gone. All
        // three are read for HEAP_MONITOR_CAPS, so they describe one and the
        // same set of heaps and can be subtracted from each other.
        //
        // The third is named min_sum because that is what the allocator
        // computes: the sum of each registered heap's own watermark, every
        // term taken at that heap's own worst instant. It is therefore a lower
        // bound on the smallest the total has ever been, not that smallest
        // value - see heap_monitor.h, and select
        // CONFIG_APRS_HEAP_REPORT_PER_HEAP to see which heap each term came
        // from.
        ESP_LOGI(TAG, "free=%u largest=%u min_sum=%u", (unsigned)heap_caps_get_free_size(HEAP_MONITOR_CAPS),
                 (unsigned)heap_caps_get_largest_free_block(HEAP_MONITOR_CAPS), (unsigned)heap_caps_get_minimum_free_size(HEAP_MONITOR_CAPS));

#if CONFIG_APRS_HEAP_REPORT_PER_HEAP
        // Start address, size, current free, largest free block and minimum
        // free ever, one row per registered heap, printed by the heap
        // component itself. It writes to stdout rather than through esp_log,
        // so it arrives as a table under the line above instead of carrying
        // its own timestamp and tag.
        heap_caps_print_heap_info(HEAP_MONITOR_CAPS);
#endif

#if CONFIG_APRS_HEAP_REPORT_TASKS
        // Per-task attribution of every live block, printed the same way and
        // for the same reason as the table above.
        heap_caps_print_all_task_stat_overview(NULL);
#endif
    }
#endif

#if CONFIG_APRS_STACK_REPORT
    if (++s_stack_elapsed_s >= CONFIG_APRS_STACK_REPORT_PERIOD_S) {
        s_stack_elapsed_s = 0;
        report_task_stacks();
    }
#endif

#if CONFIG_APRS_HEAP_INTEGRITY_CHECK
    if (++s_integrity_elapsed_s >= CONFIG_APRS_HEAP_INTEGRITY_PERIOD_S) {
        s_integrity_elapsed_s = 0;

        // The argument asks the checker to print the address of anything it
        // finds, so the failure below is a marker in the log rather than the
        // whole diagnosis: the addresses precede it.
        if (!heap_caps_check_integrity_all(true)) {
            ESP_LOGE(TAG, "heap integrity check failed, see the corrupt addresses printed above");
        }
    }
#endif
}

// Binary semaphore, not a mutex: there is no notion of ownership across
// tasks to enforce (either side may take it and the other may release it in
// principle, though in practice each caller only ever gives back what it
// took), and no priority inheritance is wanted here - a low-priority holder
// stalling briefly is fine, the whole point is that nobody blocks waiting on
// it either way.
static SemaphoreHandle_t s_heavyOpLock;

void heap_monitor_init(void) {
    if (s_heavyOpLock != NULL)
        return; // already created
    s_heavyOpLock = xSemaphoreCreateBinary();
    xSemaphoreGive(s_heavyOpLock); // starts available, not held
}

bool heap_monitor_try_heavy_op(void) {
    return xSemaphoreTake(s_heavyOpLock, 0) == pdTRUE;
}

void heap_monitor_release_heavy_op(void) {
    xSemaphoreGive(s_heavyOpLock);
}
