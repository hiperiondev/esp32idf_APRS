// @file page_logs.c
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
// @brief Web admin "Logs" page: a window that shows what the serial console
// is printing, and one button that starts and stops it.
//
// The page holds nothing to configure. It is a viewer for the mirror in
// logcapture.c: pressing Start switches the mirror on and the window
// begins filling with the same lines a serial cable would show, bounded to
// LOGCAPTURE_CAPACITY rows of at most LOGCAPTURE_LINE_MAX characters each;
// pressing Stop switches it off again. The button carries the action it will
// perform, so it reads Start while nothing is being captured and Stop while
// something is.
//
// Capture never outlives the page. The script posts /logs/stop as it loads,
// so a mirror left running by an earlier visit is switched off and the Start
// the button shows is the truth rather than a guess; leaving the page stops
// it from the browser; and a tab that is closed, put to sleep or cut off
// mid-session says nothing at all, which is why the mirror also stops itself
// once nothing has read it for LOGCAPTURE_IDLE_TIMEOUT_S. The station is
// therefore never left mirroring its log into a ring nobody is reading,
// whichever way the operator leaves.
//
// Rendering the page is inert on purpose: the stop belongs to the script and
// not to page_logs_get(), because a GET that stopped the mirror would break
// the invariant web_common.c rests its CSRF check on (no registered GET route
// has a side effect) and would put the stop out of reach of that check. It
// would also hand the effect to every way a browser fetches a URL on its own:
// a prefetch, a link prerender or an <img src> pointing at /logs would kill
// the capture the operator is watching in another window, without any script
// of this page ever running.
//
// All three endpoints behind the page - /logs/start, /logs/stop and
// /logs/read - are POST, not GET, for the reason set out in web_common.c:
// each of them changes state, so each has to come under the same-origin check
// in web_check_auth() and stay out of reach of the ways a browser fetches a
// URL by itself. That includes the polling one: reading the feed is also what
// rearms the mirror's idle timeout, which is the whole reason a capture
// outlives the second it was started in.

#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "logcapture.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// Sends a JSON document with the no-store headers every live endpoint on this
// page uses: none of these answers describes anything that is still true a
// second later, and a cached one would show a stopped capture as running or
// replay lines the operator has already seen.
static void logs_send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, json);
}

esp_err_t page_logs_start_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    bool ok = logcapture_start();

    // The sequence number the ring stands at right now is handed back as the
    // browser's starting cursor, so the window opens empty and fills with
    // what the station logs from this moment on. Reporting 0 instead would
    // pour whatever the previous capture left behind into a window the
    // operator just asked to start.
    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"seq\":%lu}", ok ? "true" : "false", (unsigned long)logcapture_latest_seq());
    logs_send_json(req, json);
    return ESP_OK;
}

esp_err_t page_logs_stop_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    logcapture_stop();
    logs_send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t page_logs_read_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    // Reading is also what keeps the mirror alive: the idle timeout is rearmed
    // here and nowhere else, so a capture lasts exactly as long as somebody
    // keeps asking for its output.
    logcapture_touch();

    uint32_t since = 0;
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "since", val, sizeof(val)) == ESP_OK)
            since = (uint32_t)strtoul(val, NULL, 10);
    }

    bool running = logcapture_is_running();

    // A cursor ahead of the ring means capture was restarted since the client
    // last polled (line numbering restarts at 1), so start again from the
    // oldest line still buffered instead of waiting for the counter to catch
    // up.
    uint32_t latest = logcapture_latest_seq();
    if (since > latest)
        since = 0;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "{\"lines\":[");

    // One byte of headroom in front of the line so the separating comma and
    // the string it precedes travel as a single chunk.
    char chunk[1 + LOGCAPTURE_JSON_LINE_MAX];
    chunk[0] = ',';

    uint32_t cursor = since;
    bool first = true;
    size_t n;
    while ((n = logcapture_next_json(cursor, latest, chunk + 1, sizeof(chunk) - 1, &cursor)) > 0) {
        httpd_resp_send_chunk(req, first ? chunk + 1 : chunk, (ssize_t)(first ? n : n + 1));
        first = false;
    }

    // "run" is what lets the page correct itself: if the mirror stopped on its
    // own while the tab was in the background, the next answer says so and the
    // button goes back to Start without the operator having to reload.
    char tail[64];
    int t = snprintf(tail, sizeof(tail), "],\"seq\":%lu,\"run\":%s}", (unsigned long)cursor, running ? "true" : "false");
    httpd_resp_send_chunk(req, tail, t);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t page_logs_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    web_send_header(req, TR_F_LOGS, "logs");

    web_fieldset_open(req, TR_LOGS_FS_CONSOLE);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_LOGS_NOTE "</p>");
    web_raw(req, "<div class='log-actions'><button type='button' id='logBtn' "
                 "onclick='logToggle()'>" TR_LOGS_BTN_START "</button></div>");
    web_raw(req, "<pre id='logBox' class='log-box'></pre>");
    web_fieldset_close(req);

    // The two button captions and the window depth are the only values the
    // script cannot hard-code: the captions are translated, and the depth is
    // the firmware's own ring size, so trimming the browser's copy of the
    // window to anything else would show a number of rows the station never
    // agreed to keep.
    {
        char vars[256];
        snprintf(vars, sizeof(vars), "<script>var LOG_START=\"%s\",LOG_STOP=\"%s\",LOG_MAX=%d;", TR_LOGS_BTN_START, TR_LOGS_BTN_STOP, LOGCAPTURE_CAPACITY);
        web_raw(req, vars);
    }

    // Polling runs on a fixed one-second tick and does nothing while the
    // button is in its Start state, so the page costs the station nothing
    // until a capture is asked for. Requests are not allowed to overlap: on a
    // busy station an answer can take longer than the interval, and without
    // the in-flight guard the queued fetches would pile up against the
    // three-connection limit the admin server runs with.
    //
    // The script's own first act is the /logs/stop that clears whatever an
    // earlier visit left running. It is fired and forgotten: the window opens
    // empty and the button reads Start regardless of the answer, and if the
    // request never lands the mirror still stops itself on its idle timeout.
    //
    // Unlike the other live pages, this one keeps polling while the tab is
    // hidden. Hiding it is precisely when an operator wants the station's
    // output to keep arriving - watching a background tab fill up while a
    // radio is being tuned in another window is the whole use for the page -
    // and the idle timeout the poll rearms is what would otherwise cut the
    // capture short.
    httpd_resp_sendstr_chunk(req, "var logRun=false,logSeq=0,logBusy=false,logLines=[];"
                                  "function logRender(){"
                                  "var b=document.getElementById('logBox');"
                                  "b.textContent=logLines.join('\\n');"
                                  "b.scrollTop=b.scrollHeight;"
                                  "}"
                                  "function logButton(){"
                                  "document.getElementById('logBtn').textContent=logRun?LOG_STOP:LOG_START;"
                                  "}"
                                  "function logPoll(){"
                                  "if(!logRun||logBusy)return;"
                                  "logBusy=true;"
                                  "fetch('/logs/"
                                  "read?since='+logSeq,{method:'POST'}).then(function(r){return "
                                  "r.json();}).then(function(v){"
                                  "if(!v.run){logRun=false;logButton();return;}"
                                  "logSeq=v.seq;"
                                  "if(v.lines&&v.lines.length){"
                                  "for(var i=0;i<v.lines.length;i++)logLines.push(v.lines[i]);"
                                  "while(logLines.length>LOG_MAX)logLines.shift();"
                                  "logRender();"
                                  "}"
                                  "}).catch(function(){}).then(function(){logBusy=false;});"
                                  "}"
                                  "function logStop(leaving){"
                                  "logRun=false;logButton();"
                                  "fetch('/logs/"
                                  "stop',{method:'POST',keepalive:!!leaving}).catch(function(){});"
                                  "}"
                                  "function logToggle(){"
                                  "if(logRun){logStop(false);return;}"
                                  "fetch('/logs/start',{method:'POST'}).then(function(r){return "
                                  "r.json();}).then(function(v){"
                                  "if(!v.ok)return;"
                                  "logSeq=v.seq;logLines=[];logRender();logRun=true;logButton();"
                                  "}).catch(function(){});"
                                  "}"
                                  "logButton();"
                                  "fetch('/logs/stop',{method:'POST'}).catch(function(){});"
                                  "var logTimer=setInterval(logPoll,1000);"
                                  "window.addEventListener('pagehide',function(){"
                                  "clearInterval(logTimer);"
                                  "if(logRun)logStop(true);"
                                  "});"
                                  "</script>");

    web_send_footer(req);
    return ESP_OK;
}
