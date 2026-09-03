// @file page_winlink.c
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
// @brief Web admin "Winlink" page: the account this station uses on the
// APRSLink service, the gating policy that lets the service reach the stations
// around this one, and a terminal for the session itself.
//
// The page carries three parts. The account fieldset holds what a session
// needs - the service callsign, the identity the mailbox is keyed on, the
// password a login challenge is answered from, and the switches that decide
// whether a session opens by itself, how long it may live and whether its
// traffic is kept off the air. The gateway fieldset holds the one setting that
// belongs to this station's other Winlink role, relaying a neighbour's session,
// together with a read-only view of the three IGate settings that decide the
// same question, so all four are visible at once instead of split across two
// pages. The session fieldset is the terminal: where the session stands, the
// commands an operator sends by hand or through the compose helper, and the
// replies the service has sent back.
//
// The account and gateway settings live in config.json like every other
// g_config field; the replies live in /storage/winlink.json, written by the
// winlink component itself. Nothing on this page keeps state of its own: the
// status and the mailbox are both polled, so two browsers watching the same
// session always agree.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "json_escape.h" // json_escape()
#include "message.h"     // APRS_MSG_TEXT_STD_MAX: the on-air limit every command field is sized to
#include "pages.h"
#include "translations.h"
#include "web_common.h"
#include "winlink.h"

static const char *TAG = "page_winlink";

// Room for the whole live-status document: the untranslated state key, the
// remaining seconds, two small counters and the escaped failure reason, whose
// width comes from the client's own WL_ERROR_MAX so the two cannot drift.
#define WL_STATUS_BUF (WL_ERROR_MAX * 2 + 160)

// Emits one label/value row of the session status table, identified by
// "wl_<key>", which is the same key the status endpoint uses for that member.
// That pairing is the whole contract between this function and
// page_winlink_status_get(), and it is what lets the page script fill the table
// without knowing what any individual row means.
static void wl_row(httpd_req_t *req, const char *label, const char *key) {
    char row[240];
    snprintf(row, sizeof(row), "<tr><td>%s</td><td id='wl_%s'>-</td></tr>", label, key);
    httpd_resp_sendstr_chunk(req, row);
}

// Emits one read-only row of the IGate settings the message gate consults. They
// are shown rather than edited here: the IGate page owns them, and an operator
// reading this page needs to know what they are set to in order to judge
// whether a reply from the service can reach the station that asked for it.
static void wl_gate_row(httpd_req_t *req, const char *label, const char *value) {
    char row[240];
    char esc[96];
    web_html_attr_escape(value, esc, sizeof(esc));
    snprintf(row, sizeof(row), "<tr><td>%s</td><td>%s</td></tr>", label, esc);
    httpd_resp_sendstr_chunk(req, row);
}

esp_err_t page_winlink_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_WINLINK, "winlink");

    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/winlink'>");

    web_fieldset_open(req, TR_WL_FS_ACCOUNT);
    httpd_resp_sendstr_chunk(req, "<p class='faint'>" TR_WL_NOTE_ACCOUNT "</p>");
    web_field_checkbox(req, TR_WL_ENABLE, "wlEnable", g_config.wl_enable);
    web_field_text(req, TR_WL_SERVICE_CALL, "wlServiceCall", g_config.wl_service_call, 9);

    // The identity the service keys the mailbox on. Mirroring the messaging
    // callsign is the ordinary case, because that callsign is what the outgoing
    // frame actually carries and therefore what the service sees; the separate
    // field exists for the station whose Winlink account is a different
    // callsign.
    web_field_checkbox(req, TR_WL_USE_MSG_CALL, "wlUseMsgCall", g_config.wl_use_msg_call);
    web_field_text(req, TR_WL_MYCALL, "wlMyCall", g_config.wl_mycall, 9);

    // Rendered as a password input with the same show/hide control the IGate
    // passcode uses. The value never travels on the air: a login challenge
    // names character positions and only those characters are quoted back.
    {
        char esc[sizeof(g_config.wl_password) * 6 + 1];
        web_html_attr_escape(g_config.wl_password, esc, sizeof(esc));
        char field[520];
        snprintf(field, sizeof(field),
                 "<label>" TR_WL_PASSWORD "</label>"
                 "<input type='password' name='wlPassword' id='wlPassword' value='%s' maxlength='%u'>"
                 "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('wlPassword',this)\"> " TR_SHOW_PASSWORD "</label>",
                 esc, (unsigned)(sizeof(g_config.wl_password) - 1));
        web_raw(req, field);
    }

    web_field_checkbox(req, TR_WL_AUTO_LOGIN, "wlAutoLogin", g_config.wl_auto_login);
    web_field_int(req, TR_WL_SESSION_MAX_MIN, "wlSessionMaxMin", g_config.wl_session_max_min, WL_SESSION_MAX_MIN_MIN, WL_SESSION_MAX_MIN_MAX);
    web_field_int(req, TR_WL_POLL_MIN, "wlPollMin", g_config.wl_poll_min, WL_POLL_MIN_MIN, WL_POLL_MIN_MAX);
    web_field_checkbox(req, TR_WL_COMMENT_EN, "wlCommentEn", g_config.wl_comment_en);
    web_field_checkbox(req, TR_WL_INET_ONLY, "wlInetOnly", g_config.wl_inet_only);
    web_fieldset_close(req);

    web_fieldset_open(req, TR_WL_FS_GATEWAY);
    httpd_resp_sendstr_chunk(req, "<p class='faint'>" TR_WL_NOTE_GATEWAY "</p>");
    web_field_checkbox(req, TR_WL_GATE_EXEMPT, "wlGateExempt", g_config.wl_gate_exempt);

    httpd_resp_sendstr_chunk(req, "<table>");
    wl_gate_row(req, TR_WL_GATE_MSG_GATE_EN, g_config.igate_msg_gate_en ? TR_ENABLED : TR_DISABLED);
    {
        char v[24];
        snprintf(v, sizeof(v), "%u", (unsigned)g_config.igate_local_window_sec);
        wl_gate_row(req, TR_WL_GATE_WINDOW, v);
        snprintf(v, sizeof(v), "%u", (unsigned)g_config.igate_msg_max_hops);
        wl_gate_row(req, TR_WL_GATE_HOPS, v);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");

    // -- Session terminal. Everything below is driven from JS against
    //    /winlink/cmd, /winlink/status and /winlink/list, so it stays outside
    //    the configuration form: a command is an action, not a setting, and
    //    must not be carried by a Save. --
    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_WL_FS_SESSION "</legend>");
    httpd_resp_sendstr_chunk(req, "<p class='faint'>" TR_WL_NOTE_SESSION "</p>");

    httpd_resp_sendstr_chunk(req, "<table>");
    wl_row(req, TR_WL_ST_STATE, "state");
    wl_row(req, TR_WL_ST_REMAINING, "remaining");
    wl_row(req, TR_WL_ST_QUEUE, "queue");
    wl_row(req, TR_WL_ST_MAILBOX, "mailbox");
    wl_row(req, TR_WL_ST_ERROR, "error");
    httpd_resp_sendstr_chunk(req, "</table>");

    httpd_resp_sendstr_chunk(req, "<p>"
                                  "<button type='button' onclick=\"wlAct('login')\">" TR_WL_BTN_LOGIN "</button> "
                                  "<button type='button' onclick=\"wlAct('list')\">" TR_WL_BTN_LIST "</button> "
                                  "<button type='button' onclick=\"wlAct('logoff')\">" TR_WL_BTN_LOGOFF "</button> "
                                  "<button type='button' onclick=\"wlAct('clear')\">" TR_WL_BTN_CLEAR_MAIL "</button>"
                                  "</p>"
                                  "<span id='wlStatusMsg' style='font-size:.8em;display:block;margin-bottom:8px;'></span>");

    // Free-form command line: the whole APRSLink command set is text, and an
    // operator who knows a command the helpers below do not cover can simply
    // type it.
    {
        char composeMax[16];
        snprintf(composeMax, sizeof(composeMax), "%d", APRS_MSG_TEXT_STD_MAX);
        httpd_resp_sendstr_chunk(req, "<div class='chat-compose'><div class='row'><div style='flex:3;'>"
                                      "<label>" TR_WL_CMD_LABEL "</label>"
                                      "<input type='text' id='wlCmdInput' maxlength='");
        httpd_resp_sendstr_chunk(req, composeMax);
        httpd_resp_sendstr_chunk(req, "' placeholder='" TR_WL_CMD_PLACEHOLDER "' "
                                      "onkeydown='if(event.key===\"Enter\"){event.preventDefault();wlSendCmd();}'>"
                                      "</div></div>"
                                      "<button type='button' onclick='wlSendCmd()'>" TR_WL_BTN_SEND "</button></div>");
    }

    // Compose helper: the three steps the service expects, in order. Each one
    // is an independent command, so an operator can also drive the same
    // sequence by hand from the field above.
    httpd_resp_sendstr_chunk(req, "<div class='chat-compose'>"
                                  "<div class='row'>"
                                  "<div><label>" TR_WL_TO "</label>"
                                  "<input type='text' id='wlTo' maxlength='60' placeholder='" TR_WL_TO_PLACEHOLDER "'></div>"
                                  "<div style='flex:2;'><label>" TR_WL_SUBJECT "</label>"
                                  "<input type='text' id='wlSubject' maxlength='60'></div>"
                                  "</div>"
                                  "<div class='row'><div style='flex:3;'><label>" TR_WL_BODY "</label>"
                                  "<input type='text' id='wlBody' maxlength='67'></div></div>"
                                  "<button type='button' onclick='wlCompose()'>" TR_WL_BTN_COMPOSE "</button> "
                                  "<button type='button' onclick='wlComposeLine()'>" TR_WL_BTN_COMPOSE_LINE "</button> "
                                  "<button type='button' onclick=\"wlAct('end')\">" TR_WL_BTN_COMPOSE_END "</button> "
                                  "<button type='button' onclick=\"wlAct('abort')\">" TR_WL_BTN_COMPOSE_ABORT "</button>"
                                  "</div>");

    httpd_resp_sendstr_chunk(req, "<h3>" TR_WL_MAILBOX "</h3>"
                                  "<div id='wlMailBox' class='chat-box'><div class='chat-empty'>" TR_WL_LOADING "</div></div>");

    httpd_resp_sendstr_chunk(req, "</fieldset>");

    // -- Inline JS: poll the status and the mailbox, post actions. Mirrors the
    //    chat page's poll pattern (short-poll + reschedule in a .catch().then()
    //    so a fetch error does not kill the loop). --
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "var WL_STATES={"
                                  "'disabled':'" TR_WL_STATE_DISABLED "','idle':'" TR_WL_STATE_IDLE "','login_sent':'" TR_WL_STATE_LOGIN_SENT "',"
                                  "'wait_challenge':'" TR_WL_STATE_WAIT_CHALLENGE "','challenge_sent':'" TR_WL_STATE_CHALLENGE_SENT "',"
                                  "'wait_valid':'" TR_WL_STATE_WAIT_VALID "','logged_in':'" TR_WL_STATE_LOGGED_IN "','composing':'" TR_WL_STATE_COMPOSING "',"
                                  "'logging_off':'" TR_WL_STATE_LOGGING_OFF "','error':'" TR_WL_STATE_ERROR "'};"
                                  "function wlEsc(s){return (s==null?'':String(s)).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
                                  "function wlSet(k,v){var e=document.getElementById('wl_'+k);if(e)e.textContent=v;}"
                                  "function wlSay(ok,text){var e=document.getElementById('wlStatusMsg');"
                                  "e.className=ok?'msg-ok':'msg-err';e.textContent=text;}"
                                  "function wlPost(params){"
                                  "return fetch('/winlink/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params})"
                                  ".then(function(r){return r.json();}).then(function(d){"
                                  "wlSay(!!(d&&d.ok),(d&&d.ok)?'" TR_WL_OK "':((d&&d.error)?d.error:'" TR_WL_FAIL "'));"
                                  "wlPollOnce();return d;})"
                                  ".catch(function(){wlSay(false,'" TR_WL_FAIL "');});}"
                                  "function wlAct(a){return wlPost('act='+encodeURIComponent(a));}"
                                  "function wlSendCmd(){var f=document.getElementById('wlCmdInput');var t=f.value.trim();"
                                  "if(!t)return;wlPost('act=cmd&text='+encodeURIComponent(t)).then(function(){f.value='';});}"
                                  "function wlCompose(){"
                                  "var to=document.getElementById('wlTo').value.trim();"
                                  "var su=document.getElementById('wlSubject').value.trim();"
                                  "if(!to)return;wlPost('act=compose&to='+encodeURIComponent(to)+'&subject='+encodeURIComponent(su));}"
                                  "function wlComposeLine(){var f=document.getElementById('wlBody');var t=f.value.trim();"
                                  "if(!t)return;wlPost('act=line&text='+encodeURIComponent(t)).then(function(){f.value='';});}"
                                  "function wlRenderStatus(d){"
                                  "if(!d)return;"
                                  "wlSet('state',WL_STATES[d.state]||d.state);"
                                  "wlSet('remaining',d.remaining>0?(d.remaining+' s'):'-');"
                                  "wlSet('queue',d.queue);"
                                  "wlSet('mailbox',d.mailbox);"
                                  "wlSet('error',d.error?d.error:'-');}"
                                  "function wlRenderMail(list){"
                                  "var box=document.getElementById('wlMailBox');"
                                  "if(!list||!list.length){box.innerHTML=\"<div class='chat-empty'>" TR_WL_EMPTY "</div>\";return;}"
                                  "var h='';"
                                  "for(var i=0;i<list.length;i++){"
                                  "var d=new Date(list[i].time*1000);"
                                  "function p(n){return (n<10?'0':'')+n;}"
                                  "var ts=p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());"
                                  "h+=\"<div class='chat-bubble rx'><span class='chat-meta'>\"+ts+\"</span>\"+wlEsc(list[i].text)+\"</div>\";}"
                                  "box.innerHTML=h;box.scrollTop=box.scrollHeight;}"
                                  "function wlPollOnce(){"
                                  "fetch('/winlink/status').then(function(r){return r.json();}).then(wlRenderStatus).catch(function(){});"
                                  "fetch('/winlink/list').then(function(r){return r.json();}).then(wlRenderMail).catch(function(){});}"
                                  "function wlPoll(){wlPollOnce();setTimeout(wlPoll,3000);}"
                                  "wlPoll();"
                                  "</script>");

    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_winlink_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[700];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    g_config.wl_enable = web_form_get_bool(body, "wlEnable");
    web_form_get_call(body, "wlServiceCall", g_config.wl_service_call, sizeof(g_config.wl_service_call));
    if (g_config.wl_service_call[0] == 0) {
        strncpy(g_config.wl_service_call, WL_SERVICE_CALL_DEFAULT, sizeof(g_config.wl_service_call) - 1);
        g_config.wl_service_call[sizeof(g_config.wl_service_call) - 1] = 0;
    }
    web_form_get(body, "wlPassword", g_config.wl_password, sizeof(g_config.wl_password));
    g_config.wl_use_msg_call = web_form_get_bool(body, "wlUseMsgCall");
    web_form_get_call(body, "wlMyCall", g_config.wl_mycall, sizeof(g_config.wl_mycall));
    g_config.wl_auto_login = web_form_get_bool(body, "wlAutoLogin");

    // Clamped here as well as in config_from_json(): the form's own min/max
    // only bind a browser, and a hand-crafted POST is exactly the case both
    // layers exist for.
    {
        int mins = web_form_get_int(body, "wlSessionMaxMin", g_config.wl_session_max_min);
        if (mins < WL_SESSION_MAX_MIN_MIN)
            mins = WL_SESSION_MAX_MIN_MIN;
        if (mins > WL_SESSION_MAX_MIN_MAX)
            mins = WL_SESSION_MAX_MIN_MAX;
        g_config.wl_session_max_min = (uint16_t)mins;

        mins = web_form_get_int(body, "wlPollMin", g_config.wl_poll_min);
        if (mins < WL_POLL_MIN_MIN)
            mins = WL_POLL_MIN_MIN;
        if (mins > WL_POLL_MIN_MAX)
            mins = WL_POLL_MIN_MAX;
        g_config.wl_poll_min = (uint16_t)mins;
    }

    g_config.wl_comment_en = web_form_get_bool(body, "wlCommentEn");
    g_config.wl_inet_only = web_form_get_bool(body, "wlInetOnly");
    g_config.wl_gate_exempt = web_form_get_bool(body, "wlGateExempt");
    app_config_unlock();

    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "Winlink settings could not be written to flash");

    // Applied either way: the values are already live in g_config, and the
    // routing of Winlink traffic must follow them even when only the flash
    // copy is stale.
    winlink_apply_config();
    web_send_save_result(req, ok, "/winlink");
    return ESP_OK;
}

esp_err_t page_winlink_cmd_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[400];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    char act[16] = { 0 };
    char text[APRS_MSG_TEXT_STD_MAX + 1] = { 0 };
    char dest[64] = { 0 };
    char subject[64] = { 0 };
    web_form_get(body, "act", act, sizeof(act));
    web_form_get(body, "text", text, sizeof(text));
    web_form_get(body, "to", dest, sizeof(dest));
    web_form_get(body, "subject", subject, sizeof(subject));

    const char *error = NULL;
    bool ok = false;

    if (!g_config.wl_enable)
        error = TR_WL_ERR_DISABLED;
    else if (strcmp(act, "login") == 0)
        ok = winlink_login();
    else if (strcmp(act, "logoff") == 0)
        ok = winlink_logoff();
    else if (strcmp(act, "list") == 0)
        ok = winlink_list();
    else if (strcmp(act, "cmd") == 0)
        ok = winlink_send_command(text);
    else if (strcmp(act, "compose") == 0)
        ok = winlink_compose_begin(dest, subject);
    else if (strcmp(act, "line") == 0)
        ok = winlink_compose_line(text);
    else if (strcmp(act, "end") == 0)
        ok = winlink_compose_end();
    else if (strcmp(act, "abort") == 0)
        ok = winlink_compose_abort();
    else if (strcmp(act, "clear") == 0)
        ok = winlink_mail_clear();
    else
        error = TR_WL_ERR_ACTION;

    if (!error && !ok)
        error = TR_WL_ERR_REFUSED;

    char resp[300];
    if (error) {
        // Translated text, so it is escaped the same way every other operator
        // string that ends up in a JSON literal is: a quote, backslash or
        // newline a translator puts in lang_*.h must not be able to break out
        // of the string it sits in.
        char error_esc[160];
        json_escape(error, error_esc, sizeof(error_esc));
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", error_esc);
    } else {
        snprintf(resp, sizeof(resp), "{\"ok\":true}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

esp_err_t page_winlink_status_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    // The reason is copied out of the session state under its lock, then
    // escaped. Twice the reason string's own width plus a terminator, since
    // json_escape() can at worst double a character it has to escape.
    char err[WL_ERROR_MAX + 1];
    winlink_last_error(err, sizeof(err));
    char err_esc[WL_ERROR_MAX * 2 + 1];
    json_escape(err, err_esc, sizeof(err_esc));

    char resp[WL_STATUS_BUF];
    snprintf(resp, sizeof(resp), "{\"state\":\"%s\",\"remaining\":%u,\"queue\":%d,\"mailbox\":%d,\"error\":\"%s\"}", winlink_state_name(winlink_state()),
             (unsigned)winlink_session_remaining_sec(), winlink_queue_depth(), winlink_mail_count(), err_esc);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

esp_err_t page_winlink_list_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");

    // The array is streamed one entry per chunk, so the whole mailbox never
    // exists in RAM at once: the peak cost is the single-entry buffer below,
    // whatever the mailbox holds. One byte of headroom in front of the entry
    // lets the separating comma and the object it precedes travel as a single
    // chunk.
    char chunk[1 + WL_MAIL_JSON_ENTRY_MAX];
    chunk[0] = ',';

    uint32_t cursor = 0;
    bool first = true;
    size_t n;
    while ((n = winlink_mail_next_json(cursor, chunk + 1, sizeof(chunk) - 1, &cursor)) > 0) {
        httpd_resp_send_chunk(req, first ? chunk + 1 : chunk, (ssize_t)(first ? n : n + 1));
        first = false;
    }

    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
