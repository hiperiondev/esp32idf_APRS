// Host renderer for the web admin screenshots: calls the real GET handler of
// every web admin route once and stores each response in <dir>/rNN.out, with
// <dir>/manifest.tsv mapping the route to its file and content type for
// serve.py.
#include "app_config.h"
#include "esp_http_server.h"
#include "lastheard.h"
#include "pages.h"
#include "sensors_local.h"
#include "trafficlog.h"
#include "web_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern char *g_out;
extern size_t g_out_len;
extern char g_ctype[64];
extern char g_query[512];
void sample_setup(void);

typedef struct {
    const char *uri;
    const char *query;
    esp_err_t (*h)(httpd_req_t *);
} route_t;
static const route_t ROUTES[] = {
    { "/", "", page_root },
    { "/dashboard", "", page_dashboard },
    { "/station", "", page_station_get },
    { "/bulletins", "", page_bulletins_get },
    { "/objects", "", page_objects_get },
    { "/sidebarInfo", "", page_sidebar_info },
    { "/igate_traffic", "since=0", page_igate_traffic },
    { "/lastheard", "", page_lastheard },
    { "/dashinfo", "", page_dashinfo },
    { "/heapinfo", "", page_heapinfo },
    { "/style.css", "", web_handle_css },
    { "/logo.png", "", web_handle_logo },
    { "/storage", "", page_storage_get },
    { "/wireless", "", page_wireless_get },
    { "/system", "", page_system_get },
    { "/about", "", page_about_get },
    { "/igate", "", page_igate_get },
    { "/bm", "", page_bm_get },
    { "/digi", "", page_digi_get },
    { "/tracker", "", page_tracker_get },
    { "/wx", "", page_wx_get },
    { "/wx/values", "ch0=255&ch1=255&ch2=255&ch3=0&ch4=255&ch5=255&ch6=255&ch7=255&ch8=0&ch9=0&ch10=255&ch11=255&ch12=255&ch13=255", page_wx_values_get },
    { "/tlm", "", page_tlm_get },
    { "/tlm/values", "", page_tlm_values_get },
    { "/gps", "", page_gps_get },
    { "/gps/values", "", page_gps_values_get },
    { "/gps/live", "", page_gps_live_get },
    { "/telegram", "", page_telegram_get },
    { "/telegram/status", "", page_telegram_status_get },
    { "/winlink", "", page_winlink_get },
    { "/winlink/status", "", page_winlink_status_get },
    { "/winlink/list", "", page_winlink_list_get },
    { "/logs", "", page_logs_get },
    { "/radio", "", page_radio_get },
    { "/msg", "", page_msg_get },
    { "/query", "", page_query_get },
    { "/msgchat", "", page_msgchat_get },
    { "/msgchat/list", "", page_msgchat_list },
    { "/symbol", "", page_symbol_get },
};

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "site";
    mkdir(dir, 0755);
    sample_setup();
    char man[256];
    snprintf(man, sizeof man, "%s/manifest.tsv", dir);
    FILE *mf = fopen(man, "w");
    for (size_t i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++) {
        g_out_len = 0;
        if (g_out)
            g_out[0] = 0;
        strcpy(g_ctype, "text/html");
        snprintf(g_query, sizeof g_query, "%s", ROUTES[i].query);
        httpd_req_t r = { .uri = ROUTES[i].uri, .method = HTTP_GET };
        ROUTES[i].h(&r);
        char fn[256];
        snprintf(fn, sizeof fn, "%s/r%02zu.out", dir, i);
        FILE *f = fopen(fn, "wb");
        if (g_out_len)
            fwrite(g_out, 1, g_out_len, f);
        fclose(f);
        fprintf(mf, "%s\tr%02zu.out\t%s\n", ROUTES[i].uri, i, g_ctype);
    }
    fclose(mf);
    return 0;
}
