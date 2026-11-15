// The sample station the screenshots show: factory defaults from
// app_config_set_defaults(), a callsign and position, a few enabled services,
// one bulletin, one object, and some received traffic in the dashboard ring.
#include "app_config.h"
#include "bulletins.h"
#include "lastheard.h"
#include "objects_items.h"
#include "sensors_local.h"
#include "telemetry.h"
#include "trafficlog.h"
#include <stdio.h>
#include <string.h>
#define S(f, v) snprintf(g_config.f, sizeof(g_config.f), "%s", v)
void sample_setup(void) {
    app_config_set_defaults(&g_config);
    sensors_local_init();
    sensors_local_init_all();
    trafficlog_init();
    lastheard_init();
    S(my_callsign, "LU3VEA");
    g_config.my_lat = -38.7183f;
    g_config.my_lon = -62.2663f;
    g_config.my_alt = 21;
    S(aprs_mycall, "LU3VEA");
    S(aprs_passcode, "12345");
    S(aprs_filter, "m/50");
    S(digi_mycall, "LU3VEA");
    S(trk_mycall, "LU3VEA");
    S(wx_mycall, "LU3VEA");
    S(msg_mycall, "LU3VEA");
    g_config.igate_lat = -38.7183f;
    g_config.igate_lon = -62.2663f;
    g_config.igate_alt = 21;
    g_config.digi_lat = -38.7183f;
    g_config.digi_lon = -62.2663f;
    g_config.wx_lat = -38.7183f;
    g_config.wx_lon = -62.2663f;
    g_config.trk_lat = -38.7183f;
    g_config.trk_lon = -62.2663f;
    g_config.digi_en = true;
    g_config.wx_en = true;
    g_config.gps_en = true;
    g_config.wifi_mode = WIFI_MODE_CFG_APSTA;
    g_config.wifi_sta[0].enable = true;
    S(wifi_sta[0].wifi_ssid, "HomeNet");
    g_config.timezone_idx = 0;
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        g_config.wx_sensor_ch[i] = SENSOR_LOCAL_CH_NONE;
    g_config.wx_sensor_ch[WX_FIELD_TEMPERATURE] = 0;
    g_config.wx_sensor_ch[WX_FIELD_HUMIDITY] = 0;
    g_config.wx_sensor_ch[WX_FIELD_PRESSURE] = 0;

    bulletins_t b;
    memset(&b, 0, sizeof b);
    bulletins_load(&b);
    b.item[0].enable = true;
    b.item[0].send_rf = true;
    b.item[0].send_inet = true;
    b.item[0].ident = '0';
    snprintf(b.item[0].text, sizeof b.item[0].text, "Club net tonight 21:00 local on 147.300");
    b.item[0].interval_s = 600;
    b.item[0].slow_interval_s = 3600;
    b.item[0].decay_x10 = 20;
    b.item[0].expire_hours = 24;
    (void)bulletins_save(&b);
    objitems_t o;
    memset(&o, 0, sizeof o);
    objitems_load(&o);
    o.item[0].enable = true;
    o.item[0].send_rf = true;
    o.item[0].send_inet = true;
    o.item[0].active = true;
    snprintf(o.item[0].name, sizeof o.item[0].name, "147.300BB");
    o.item[0].lat = -38.70f;
    o.item[0].lon = -62.27f;
    o.item[0].sym[0] = '/';
    o.item[0].sym[1] = 'r';
    o.item[0].freq_mhz = 147.300f;
    o.item[0].duplex = 1;
    o.item[0].offset_khz = 600;
    o.item[0].tone_tenths = 1000;
    snprintf(o.item[0].comment, sizeof o.item[0].comment, "Club repeater");
    (void)objitems_save(&o);
    (void)app_config_save();

    lastheard_add("LU1ABC-9", "WIDE1-1,WIDE2-1", true, true, 0, '/', '>', false);
    lastheard_add("LU7DEF-10", "TCPIP*,qAC,T2BRAZIL", false, false, 0, '/', '&', false);
    lastheard_add("LW2GHI", "LU3VEA-1*,WIDE2-1", true, false, 1, '/', '-', false);
    lastheard_add("LU4JKL-13", "WIDE2-1", true, true, 0, '/', '_', false);
    trafficlog_add_pkt("RX", "LU1ABC-9", "LU1ABC-9>APDR16,WIDE1-1,WIDE2-1:=3843.55S/06215.41W>273/032/A=000072", "273° 32 kn, 72 ft", 312, '/', '>');
    trafficlog_add_pkt("RX", "LU4JKL-13", "LU4JKL-13>APRS,WIDE2-1:_11151208c220s004g008t064r000p000h63b10132", "", 287, '/', '_');
    trafficlog_add_pkt("DIGI", "LW2GHI", "LW2GHI>APRS,LU3VEA-1*,WIDE2-1:!3846.20S/06210.10W-QTH", "", 0, '/', '-');
    trafficlog_add_pkt("TX", "LU3VEA-10", "LU3VEA-10>APESP1,WIDE1-1:!3843.10SN06216.00W&esp32idf_APRS IGate", "", 0, 'N', '&');
    trafficlog_add_pkt("RX-IS", "LU7DEF-10", "LU7DEF-10>APRX29,TCPIP*,qAC,T2BRAZIL:>Bahia Blanca iGate", "", 0, '/', '&');
}
