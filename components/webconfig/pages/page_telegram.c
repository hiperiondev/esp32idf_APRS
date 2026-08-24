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
// @brief Web admin "Telegram" page: the bot's enable switch, the two settings
// an operator has to be able to correct from a browser (the token and the
// administrator's identifier), and a live diagnosis of where the connection to
// api.telegram.org currently stands.
//
// Everything on this page is stored in /storage/telegram.json, not in
// config.json, so the whole bot configuration is one file that can be
// downloaded, edited and uploaded again from the File Storage page. The parts
// this page does not render - the Mini App address and the user/chat lists -
// are loaded into the same structure before a save and written back untouched.

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
    web_raw(req, "<p style='color:var(--sub);font-size:12px;margin:4px 0'>" TR_TG_NOTE_ADMIN "</p>");
    web_fieldset_close(req);

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

    // The parts of the file this page does not edit are named here rather
    // than rendered as fields: the Mini App address and the user and chat
    // lists are edited by uploading the file, and an operator looking for
    // them needs to be told where they are instead of concluding they are
    // unsupported.
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

esp_err_t page_telegram_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[512];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Loaded first so the members this page does not render - the Mini App
    // address and the user and chat lists - are carried through the save
    // instead of being erased by a form that never knew about them.
    telegram_app_config_t cfg;
    telegram_app_load(&cfg);

    cfg.enable = web_form_get_bool(body, "tgEn");
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
