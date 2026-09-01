// @file page_telegram.c
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
// @brief Web admin "Telegram" page: the bot's enable switch, the switch that
// routes incoming station messages to the bot, the switch that routes
// incoming bulletins to it together with the window an identical bulletin
// stays suppressed for, the settings an operator has to be able to
// correct from a browser (the token, the administrator's identifier, the Mini
// App address, the authorized users and the allowed group chats), and a live
// diagnosis of where the connection to api.telegram.org currently stands.
//
// Everything on this page is stored in /storage/telegram.json, not in
// config.json, so the whole bot configuration is one file that can also be
// downloaded, edited and uploaded again from the File Storage page. The
// authorized users and allowed group chats are fixed-size tables
// (TELEGRAM_APP_USERS_MAX / TELEGRAM_APP_CHATS_MAX entries), each rendered as
// one accordion card; a card whose identifier is left at 0 is an empty slot
// and is dropped from the table on save, the same way a hand-edited file
// omits it. Each user's card also carries that operator's own callsign, which
// is the addressee "Route Station messages" matches an incoming APRS message
// against to decide which user's Telegram account it is delivered to.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "json_escape.h" // json_escape()
#include "pages.h"
#include "str_append.h" // str_append(), str_append_truncated()
#include "telegram_app.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_telegram";

// Room for the whole live-status document. Its widest form carries the
// translated state and reason phrases, a detail string bounded by
// TELEGRAM_APP_DETAIL_MAX, the bot name and six counters; the longest of the
// three languages measures a little over 600 bytes, so this leaves room for
// rows added later and makes the truncation branch below a guard rather than
// an expected path.
#define TELEGRAM_STATUS_BUF 1024

// Emits one label/value row of the live status table, identified by
// "tg_<key>", which is the same key the status endpoint uses for that member.
// That pairing is the whole contract between this function and
// telegram_status_json(), and it is what lets the page script fill the table
// without knowing what any individual row means.
static void tg_row(httpd_req_t *req, const char *label, const char *key) {
    char row[240];
    snprintf(row, sizeof(row), "<tr><td>%s</td><td id='tg_%s'>-</td></tr>", label, key);
    httpd_resp_sendstr_chunk(req, row);
}

// Renders one accordion card of the allowed-chats table: an identifier field
// and a display-name field, under field names built from id_prefix plus the
// slot index (e.g. "tgCId3"/"tgCName3"). peer is NULL for a slot past the
// stored entry count, which renders as an empty card - the same shape a slot
// cleared by the operator ends up in after a save.
//
// Shares the .achan accordion styling and the accordionClick() helper with
// the Bulletins and Objects/Items pages, and with tg_render_user() below;
// id_prefix doubles as the accordionClick() DOM-id prefix, so the users and
// chats tables toggle independently of each other and of any other accordion
// on the page.
static void tg_render_peer(httpd_req_t *req, const char *id_prefix, int index, int count, const char *legend_fmt, const telegram_app_peer_t *peer) {
    char legend[32];
    snprintf(legend, sizeof(legend), legend_fmt, index + 1);

    char head[256];
    snprintf(head, sizeof(head),
             "<div class='achan%s' id='%s%d'>"
             "<div class='achan-head' onclick='accordionClick(\"%s\",%d,%d)'>"
             "<span class='achan-name'>%s</span>"
             "<span class='achan-caret'>&#9654;</span>"
             "</div><div class='achan-body'>",
             (index == 0) ? " open" : "", id_prefix, index, id_prefix, index, count, legend);
    httpd_resp_sendstr_chunk(req, head);

    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%lld", (long long)(peer ? peer->id : 0));
    char name[24];
    snprintf(name, sizeof(name), "%sId%d", id_prefix, index + 1);
    web_field_text(req, TR_TG_F_PEER_ID, name, idbuf, 20);

    snprintf(name, sizeof(name), "%sName%d", id_prefix, index + 1);
    web_field_text(req, TR_TG_F_PEER_NAME, name, peer ? peer->name : "", TELEGRAM_APP_NAME_MAX);

    httpd_resp_sendstr_chunk(req, "</div></div>");
}

// Renders one accordion card of the authorized-users table: an identifier
// field, a display-name field and, unlike tg_render_peer(), the operator's
// own callsign field, which is the addressee "Route Station messages" matches
// an incoming APRS message against to reach this user. user is NULL for a slot past the stored entry
// count, which renders as an empty card - the same shape a slot cleared by
// the operator ends up in after a save.
static void tg_render_user(httpd_req_t *req, const char *id_prefix, int index, int count, const char *legend_fmt, const telegram_app_user_t *user) {
    char legend[32];
    snprintf(legend, sizeof(legend), legend_fmt, index + 1);

    char head[256];
    snprintf(head, sizeof(head),
             "<div class='achan%s' id='%s%d'>"
             "<div class='achan-head' onclick='accordionClick(\"%s\",%d,%d)'>"
             "<span class='achan-name'>%s</span>"
             "<span class='achan-caret'>&#9654;</span>"
             "</div><div class='achan-body'>",
             (index == 0) ? " open" : "", id_prefix, index, id_prefix, index, count, legend);
    httpd_resp_sendstr_chunk(req, head);

    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%lld", (long long)(user ? user->id : 0));
    char name[24];
    snprintf(name, sizeof(name), "%sId%d", id_prefix, index + 1);
    web_field_text(req, TR_TG_F_PEER_ID, name, idbuf, 20);

    snprintf(name, sizeof(name), "%sName%d", id_prefix, index + 1);
    web_field_text(req, TR_TG_F_PEER_NAME, name, user ? user->name : "", TELEGRAM_APP_NAME_MAX);

    snprintf(name, sizeof(name), "%sCallsign%d", id_prefix, index + 1);
    web_field_text(req, TR_TG_F_USER_CALLSIGN, name, user ? user->callsign : "", TELEGRAM_APP_CALLSIGN_MAX);

    httpd_resp_sendstr_chunk(req, "</div></div>");
}

// Translated one-word rendering of the coarse state.
static const char *state_text(telegram_app_state_t state) {
    switch (state) {
        case TELEGRAM_APP_STATE_STARTING:
            return TR_TG_STATE_STARTING;
        case TELEGRAM_APP_STATE_RUNNING:
            return TR_TG_STATE_RUNNING;
        case TELEGRAM_APP_STATE_ERROR:
            return TR_TG_STATE_ERROR;
        case TELEGRAM_APP_STATE_DISABLED:
        default:
            return TR_TG_STATE_DISABLED;
    }
}

// Translated explanation of the precise cause, in the operator's language.
//
// Each of these names one thing to check and, where the fault has a remedy the
// operator can carry out from this web admin, says which page to carry it out
// on. That is the whole point of reporting a reason rather than an error code:
// the codes the transport and the TLS stack produce are accurate and useless
// on their own.
static const char *reason_text(telegram_app_reason_t reason) {
    switch (reason) {
        case TELEGRAM_APP_REASON_FILE_MISSING:
            return TR_TG_R_FILE_MISSING;
        case TELEGRAM_APP_REASON_FILE_CORRUPT:
            return TR_TG_R_FILE_CORRUPT;
        case TELEGRAM_APP_REASON_FILE_UNREADABLE:
            return TR_TG_R_FILE_UNREADABLE;
        case TELEGRAM_APP_REASON_NO_TOKEN:
            return TR_TG_R_NO_TOKEN;
        case TELEGRAM_APP_REASON_TOKEN_MALFORMED:
            return TR_TG_R_TOKEN_MALFORMED;
        case TELEGRAM_APP_REASON_CERT_MISSING:
            return TR_TG_R_CERT_MISSING;
        case TELEGRAM_APP_REASON_CERT_INVALID:
            return TR_TG_R_CERT_INVALID;
        case TELEGRAM_APP_REASON_WAITING_NETWORK:
            return TR_TG_R_WAITING_NETWORK;
        case TELEGRAM_APP_REASON_DNS_FAILED:
            return TR_TG_R_DNS_FAILED;
        case TELEGRAM_APP_REASON_TCP_FAILED:
            return TR_TG_R_TCP_FAILED;
        case TELEGRAM_APP_REASON_NO_MEMORY:
            return TR_TG_R_NO_MEMORY;
        case TELEGRAM_APP_REASON_INIT_FAILED:
            return TR_TG_R_INIT_FAILED;
        case TELEGRAM_APP_REASON_CONNECT_FAILED:
            return TR_TG_R_CONNECT_FAILED;
        case TELEGRAM_APP_REASON_API_REJECTED:
            return TR_TG_R_API_REJECTED;
        case TELEGRAM_APP_REASON_TASK_FAILED:
            return TR_TG_R_TASK_FAILED;
        case TELEGRAM_APP_REASON_CONNECTED:
            return TR_TG_R_CONNECTED;
        case TELEGRAM_APP_REASON_DISABLED:
        default:
            return TR_TG_R_DISABLED;
    }
}

// Builds the live-status document into out.
//
// Every member is either a quoted, ready-to-display string or the literal
// null, and a member is emitted as null exactly when the value does not apply
// yet - which the page renders as "-". Formatting happens here rather than in
// the browser so the units and the translated phrases stay with the rest of
// the firmware's strings.
//
// One member does come from outside this firmware: the detail carries a file
// path, an esp_err_t name or the description api.telegram.org returned, so it
// is escaped before it goes into the document. Everything else is either a
// number this firmware formatted or one of the translated literals above.
static void telegram_status_json(const telegram_app_status_t *st, char *out, size_t out_size) {
    size_t used = 0;

    str_append(out, out_size, &used, "{");
    str_append(out, out_size, &used, "\"state\":\"%s\"", state_text(st->state));
    str_append(out, out_size, &used, ",\"reason\":\"%s\"", reason_text(st->reason));

    if (st->detail[0]) {
        char esc[sizeof(st->detail) * 6 + 1];
        json_escape(st->detail, esc, sizeof(esc));
        str_append(out, out_size, &used, ",\"detail\":\"%s\"", esc);
    } else {
        str_append(out, out_size, &used, ",\"detail\":null");
    }

    if (st->bot_name[0]) {
        char esc[sizeof(st->bot_name) * 6 + 1];
        json_escape(st->bot_name, esc, sizeof(esc));
        str_append(out, out_size, &used, ",\"bot\":\"@%s\"", esc);
    } else {
        str_append(out, out_size, &used, ",\"bot\":null");
    }

    if (st->has_counters) {
        str_append(out, out_size, &used, ",\"uptime\":\"%lld s\"", (long long)st->uptime_seconds);
        str_append(out, out_size, &used, ",\"updates\":\"%u\"", (unsigned)st->updates_received);
        str_append(out, out_size, &used, ",\"commands\":\"%u\"", (unsigned)st->commands_handled);
        str_append(out, out_size, &used, ",\"sent\":\"%u\"", (unsigned)st->messages_sent);
        str_append(out, out_size, &used, ",\"rejected\":\"%u\"", (unsigned)st->rejected);
        str_append(out, out_size, &used, ",\"pollErrors\":\"%u\"", (unsigned)st->poll_errors);
    } else {
        str_append(out, out_size, &used, ",\"uptime\":null,\"updates\":null,\"commands\":null,\"sent\":null,\"rejected\":null,\"pollErrors\":null");
    }

    str_append(out, out_size, &used, "}");

    if (str_append_truncated(used, out_size)) {
        ESP_LOGE(TAG, "status document did not fit %u bytes", (unsigned)out_size);
        snprintf(out, out_size, "{}");
    }
}

esp_err_t page_telegram_status_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    telegram_app_status_t st;
    telegram_app_status(&st);

    char json[TELEGRAM_STATUS_BUF];
    telegram_status_json(&st, json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

esp_err_t page_telegram_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_TELEGRAM, "telegram");

    telegram_app_config_t cfg;
    telegram_app_load(&cfg);

    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/telegram'>");

    // SERVICE -------------------------------------------------------------
    web_fieldset_open(req, TR_TG_FS_SERVICE);
    web_field_checkbox(req, TR_TG_ENABLE, "tgEn", cfg.enable);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_SERVICE "</p>");
    web_field_checkbox(req, TR_TG_ROUTE_MESSAGES, "tgRouteMsg", cfg.route_station_messages);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_ROUTE_MESSAGES "</p>");
    web_field_checkbox(req, TR_TG_ROUTE_BULLETINS, "tgRouteBul", cfg.route_bulletins);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_ROUTE_BULLETINS "</p>");
    web_field_int(req, TR_TG_BULLETIN_WINDOW, "tgBulWin", (long)cfg.bulletin_window_s, TELEGRAM_APP_BULLETIN_WINDOW_MIN, TELEGRAM_APP_BULLETIN_WINDOW_MAX);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_BULLETIN_WINDOW "</p>");
    web_fieldset_close(req);

    // CREDENTIALS ---------------------------------------------------------
    // The token is rendered as a password field with the same show/hide
    // control the IGate passcode and the admin password use: it is a bearer
    // credential, and anyone who reads it off a screen can send messages as
    // this bot until it is revoked.
    web_fieldset_open(req, TR_TG_FS_BOT);
    {
        char esc[sizeof(cfg.bot_token) * 6 + 1];
        web_html_attr_escape(cfg.bot_token, esc, sizeof(esc));
        char buf[sizeof(esc) + 600];
        snprintf(buf, sizeof(buf),
                 "<label>%s</label>"
                 "<input type='password' name='tgToken' id='tgToken' value='%s' maxlength='%d'>"
                 "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('tgToken',this)\"> " TR_SHOW_PASSWORD "</label>",
                 TR_TG_TOKEN, esc, (int)(sizeof(cfg.bot_token) - 1));
        web_raw(req, buf);
    }
    // Rendered as text, not as a number field: Telegram identifiers are 64-bit
    // and today's user identifiers already exceed the range of the long this
    // firmware's numeric field helper carries, so a numeric input would round
    // or reject the very identifiers it exists to accept.
    {
        char idbuf[24];
        snprintf(idbuf, sizeof(idbuf), "%lld", (long long)cfg.admin_id);
        web_field_text(req, TR_TG_ADMIN_ID, "tgAdmin", idbuf, 20);
    }
    // Shown only while it applies, and only for the state an operator can act
    // on from this very fieldset. A bot with a token but no administrator is
    // a running service that answers nobody, which from the Telegram client
    // looks the same as a bot that is off or unreachable; saying so here is
    // what turns that silence into one field left to fill in.
    if (cfg.enable && cfg.admin_id == 0) {
        web_raw(req, "<p style='background:#fef3c7;color:#92400e;font-size:12px;margin:8px 0;padding:8px 10px;border-radius:6px'>" TR_TG_WARN_NO_ADMIN "</p>");
    }
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_ADMIN "</p>");
    web_fieldset_close(req);

    // MINI APP --------------------------------------------------------------
    web_fieldset_open(req, TR_TG_FS_MINIAPP);
    {
        char esc[sizeof(cfg.web_app_url) * 6 + 1];
        web_html_attr_escape(cfg.web_app_url, esc, sizeof(esc));
        char buf[sizeof(esc) + 200];
        snprintf(buf, sizeof(buf), "<label>%s</label><input type='text' name='tgUrl' value='%s' maxlength='%d'>", TR_TG_MINIAPP_URL, esc,
                 (int)(sizeof(cfg.web_app_url) - 1));
        web_raw(req, buf);
    }
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_MINIAPP "</p>");
    web_fieldset_close(req);

    // AUTHORIZED USERS ------------------------------------------------------
    web_fieldset_open(req, TR_TG_FS_USERS);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_USERS "</p>");
    httpd_resp_sendstr_chunk(req, "<div id='tgUWrap'>");
    for (int i = 0; i < TELEGRAM_APP_USERS_MAX; i++)
        tg_render_user(req, "tgU", i, TELEGRAM_APP_USERS_MAX, TR_TG_F_USER_FMT, (i < cfg.user_count) ? &cfg.users[i] : NULL);
    httpd_resp_sendstr_chunk(req, "</div>");
    web_fieldset_close(req);

    // ALLOWED GROUP CHATS ---------------------------------------------------
    web_fieldset_open(req, TR_TG_FS_CHATS);
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_CHATS "</p>");
    httpd_resp_sendstr_chunk(req, "<div id='tgCWrap'>");
    for (int i = 0; i < TELEGRAM_APP_CHATS_MAX; i++)
        tg_render_peer(req, "tgC", i, TELEGRAM_APP_CHATS_MAX, TR_TG_F_CHAT_FMT, (i < cfg.chat_count) ? &cfg.chats[i] : NULL);
    httpd_resp_sendstr_chunk(req, "</div>");
    web_fieldset_close(req);

    // Generic single-open accordion helper, shared verbatim with the
    // Bulletins and Objects/Items pages: closes every card of the given
    // id-prefix except the one just clicked (toggling it), so at most one
    // card of the users table and at most one card of the chats table are
    // expanded at a time, independently of each other.
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function accordionClick(p,i,n){"
                                  "for(var k=0;k<n;k++){"
                                  "var c=document.getElementById(p+k);"
                                  "if(!c)continue;"
                                  "if(k===i)c.classList.toggle('open');"
                                  "else c.classList.remove('open');"
                                  "}"
                                  "}"
                                  "</script>");

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");

    // STATUS --------------------------------------------------------------
    // Read-only and outside the form: these are observations about a live
    // connection, not settings, and putting them inside would invite a Save
    // that appears to store them.
    web_fieldset_open(req, TR_TG_FS_STATUS);
    httpd_resp_sendstr_chunk(req, "<table>");
    tg_row(req, TR_TG_ST_STATE, "state");
    tg_row(req, TR_TG_ST_REASON, "reason");
    tg_row(req, TR_TG_ST_DETAIL, "detail");
    tg_row(req, TR_TG_ST_BOT, "bot");
    tg_row(req, TR_TG_ST_UPTIME, "uptime");
    tg_row(req, TR_TG_ST_UPDATES, "updates");
    tg_row(req, TR_TG_ST_COMMANDS, "commands");
    tg_row(req, TR_TG_ST_SENT, "sent");
    tg_row(req, TR_TG_ST_REJECTED, "rejected");
    tg_row(req, TR_TG_ST_POLL_ERRORS, "pollErrors");
    httpd_resp_sendstr_chunk(req, "</table>");
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_STATUS "</p>");
    web_fieldset_close(req);

    // Named here so an operator who prefers to hand-edit the whole
    // configuration at once knows where it lives: everything on this page is
    // one file that can also be downloaded, edited and uploaded again from
    // the File Storage page.
    {
        char note[400];
        snprintf(note, sizeof(note), "<p style='color:var(--sub);font-size:12px;margin:4px 0'>%s <code>%s</code></p>", TR_TG_NOTE_FILE, TELEGRAM_APP_PATH);
        web_raw(req, note);
    }

    // Live refresh: one fetch every five seconds, writing each member of the
    // returned object into the cell whose id is "tg_" plus that member's key.
    // Driving the update from the response's own keys - rather than from a
    // list of row names repeated here - means a row added above needs no
    // change in this script, and a member the firmware omits leaves its cell
    // showing the placeholder instead of a stale reading.
    //
    // Requests are not allowed to overlap: on a busy station a response can
    // take longer than the interval, and without the in-flight guard the
    // queued fetches would pile up against the four-connection limit the admin
    // server runs with. Polling stops when the page is hidden and resumes when
    // it is shown again, so a tab left open in the background does not keep
    // the station answering requests nobody is reading.
    //
    // Five seconds rather than one: this page is most often watched while the
    // bot is trying to connect, which is exactly when the station is least
    // able to spare transmit buffers. A status table that updates less often
    // is a better trade than a page whose own polling competes with the
    // handshake it is reporting on.
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "var tgBusy=false;"
                                  "function tgRefresh(){"
                                  "if(tgBusy||document.hidden)return;"
                                  "tgBusy=true;"
                                  "fetch('/telegram/status').then(function(r){return r.json();}).then(function(v){"
                                  "for(var k in v){"
                                  "var td=document.getElementById('tg_'+k);"
                                  "if(td)td.textContent=(v[k]===null||v[k]===undefined)?'-':v[k];"
                                  "}"
                                  "}).catch(function(){}).then(function(){tgBusy=false;});"
                                  "}"
                                  "tgRefresh();"
                                  "var tgTimer=setInterval(tgRefresh,5000);"
                                  "document.addEventListener('visibilitychange',function(){if(!document.hidden)tgRefresh();});"
                                  "window.addEventListener('beforeunload',function(){clearInterval(tgTimer);});"
                                  "</script>");

    web_send_footer(req);
    return ESP_OK;
}

// Reads one accordion card's identifier field back out of the POST body,
// shared by the authorized-users and allowed-chats tables since both name it
// the same way ("<id_prefix>Id<slot>"; see tg_render_peer() / tg_render_user()).
// An identifier field left empty, or holding anything that is not a number,
// reads as 0, which the caller treats as an empty slot - the same value
// load_peers() / load_users() in telegram_app.c drop rather than storing.
static int64_t tg_parse_peer_id(const char *body, const char *id_prefix, int index) {
    char name[24];
    snprintf(name, sizeof(name), "%sId%d", id_prefix, index + 1);
    char idbuf[24];
    if (!web_form_get(body, name, idbuf, sizeof(idbuf)))
        return 0;
    return (int64_t)strtoll(idbuf, NULL, 10);
}

// Reads the whole allowed-chats table back into out, compacting the result: a
// slot whose identifier field was left at 0 is skipped rather than stored as
// a zero-identifier entry, so clearing a card in the middle of the table
// removes that entry instead of leaving a hole telegram_service would
// otherwise have to special-case.
static uint8_t tg_parse_peer_table(const char *body, const char *id_prefix, int count, telegram_app_peer_t *out) {
    uint8_t used = 0;
    for (int i = 0; i < count; i++) {
        int64_t id = tg_parse_peer_id(body, id_prefix, i);
        if (id == 0)
            continue;
        char name[24];
        snprintf(name, sizeof(name), "%sName%d", id_prefix, i + 1);
        out[used].id = id;
        web_form_get(body, name, out[used].name, sizeof(out[used].name));
        used++;
    }
    return used;
}

// Trims leading and trailing whitespace off s and upper-cases what remains,
// in place. Applied to a saved callsign field so it is stored in exactly the
// form telegram_app_notify_station_message() compares a routed message's
// addressee against.
static void tg_trim_upper(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    for (size_t i = 0; i < len; i++)
        s[i] = (char)toupper((unsigned char)s[i]);
}

// Reads the whole authorized-users table back into out, the same way
// tg_parse_peer_table() reads the allowed-chats table, plus each entry's own
// callsign, normalized to upper case with tg_trim_upper().
static uint8_t tg_parse_user_table(const char *body, const char *id_prefix, int count, telegram_app_user_t *out) {
    uint8_t used = 0;
    for (int i = 0; i < count; i++) {
        int64_t id = tg_parse_peer_id(body, id_prefix, i);
        if (id == 0)
            continue;
        char name[24];
        snprintf(name, sizeof(name), "%sName%d", id_prefix, i + 1);
        out[used].id = id;
        web_form_get(body, name, out[used].name, sizeof(out[used].name));

        snprintf(name, sizeof(name), "%sCallsign%d", id_prefix, i + 1);
        web_form_get(body, name, out[used].callsign, sizeof(out[used].callsign));
        tg_trim_upper(out[used].callsign);

        used++;
    }
    return used;
}

esp_err_t page_telegram_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char *body = malloc(WEBCONFIG_POST_BUF_TELEGRAM);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, WEBCONFIG_POST_BUF_TELEGRAM) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Loaded first so a field this save omits - which does not happen today,
    // since this page now renders every member of the file, but would for a
    // form submitted with a stale field set - is carried through rather than
    // erased.
    telegram_app_config_t cfg;
    telegram_app_load(&cfg);

    cfg.enable = web_form_get_bool(body, "tgEn");
    cfg.route_station_messages = web_form_get_bool(body, "tgRouteMsg");
    cfg.route_bulletins = web_form_get_bool(body, "tgRouteBul");

    // Clamped here rather than trusted: the min/max the field carries are
    // browser-side validation, which a hand-crafted POST does not go through,
    // and a window read straight from the body would otherwise become the
    // interval the routing path measures against.
    {
        int window = web_form_get_int(body, "tgBulWin", TELEGRAM_APP_BULLETIN_WINDOW_DEFAULT);
        if (window < TELEGRAM_APP_BULLETIN_WINDOW_MIN)
            window = TELEGRAM_APP_BULLETIN_WINDOW_MIN;
        if (window > TELEGRAM_APP_BULLETIN_WINDOW_MAX)
            window = TELEGRAM_APP_BULLETIN_WINDOW_MAX;
        cfg.bulletin_window_s = (uint32_t)window;
    }

    web_form_get(body, "tgToken", cfg.bot_token, sizeof(cfg.bot_token));

    // strtoll, not the int helper: a Telegram user identifier does not fit in
    // the long that helper returns on this target. A field left empty or
    // holding anything that is not a number stores 0, which is what "no
    // administrator" means to the service.
    {
        char idbuf[24];
        if (web_form_get(body, "tgAdmin", idbuf, sizeof(idbuf)))
            cfg.admin_id = (int64_t)strtoll(idbuf, NULL, 10);
        else
            cfg.admin_id = 0;
    }

    web_form_get(body, "tgUrl", cfg.web_app_url, sizeof(cfg.web_app_url));

    cfg.user_count = tg_parse_user_table(body, "tgU", TELEGRAM_APP_USERS_MAX, cfg.users);
    cfg.chat_count = tg_parse_peer_table(body, "tgC", TELEGRAM_APP_CHATS_MAX, cfg.chats);

    free(body);

    bool ok = telegram_app_save(&cfg);
    if (!ok)
        ESP_LOGE(TAG, "Telegram settings could not be written to flash");

    // Applied after the save, unlike the GPS switch: bring-up re-reads the
    // file, so applying it before the write would start the bot from settings
    // that are not on the filesystem yet and would survive a reboot as
    // something else entirely.
    telegram_app_apply_config();

    web_send_save_result(req, ok, "/telegram");
    return ESP_OK;
}
