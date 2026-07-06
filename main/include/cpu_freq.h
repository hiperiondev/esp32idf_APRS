#pragma once

// Applies g_config.cpuFreq (80/160/240 MHz) to the running system via
// esp_pm_configure(), so the "CPU frequency" setting on the System web page
// actually changes the real clock speed instead of only being stored/shown.
// Safe to call at boot (after app_config_load()) and again any time the
// setting is changed and saved (e.g. from page_system_post()) to apply it
// immediately without requiring a reboot.
void cpu_freq_apply(void);
