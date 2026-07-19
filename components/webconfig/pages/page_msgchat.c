/**
 * @file page_msgchat.c
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
 * @brief Web admin "Snd/Rcv Msg" page: a chat-style APRS messaging UI - a
 * scrolling panel of received/sent messages for this station (as configured
 * on the Station / Message pages), a destination-callsign field, a
 * message-text field capped at the standard APRS message length, and a Send
 * button. Distinct from page_msg.c, which only configures the messaging
 * feature (RF/INET enable, retry, encryption) - this page is the actual
 * inbox/compose UI built on top of that configuration. Gated from the
 * sidebar by ENABLE_MSG_CHAT in app_config.h's MODULES section.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "message.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

// GET /msgchat -> renders the chat page shell; the message list itself is
// filled in by JS polling GET /msgchat/list, same live-refresh pattern as
// the dashboard's IGate Traffic table.
esp_err_t page_msgchat_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_SND_RCV_MSG, "msgchat");

    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_F_SND_RCV_MSG "</legend>");

    // "My Station" identity: this mirrors g_config.msg_mycall, which is
    // itself copied from the Station page's callsign whenever "Use My
    // Station Data" is checked on the Message config page (g_config.
    // msg_use_station) - see page_msg.c/page_station.c. This is the address
    // handleIncomingAPRS() actually matches inbound messages against, so
    // it's the identity that matters here rather than g_config.my_callsign
    // directly.
    char myStationLine[220];
    snprintf(myStationLine, sizeof(myStationLine), "<p><label style='display:inline;margin:0;'>" TR_MSGCHAT_MY_STATION "</label> <b>%s</b></p>",
             g_config.msg_mycall[0] ? g_config.msg_mycall : "-");
    httpd_resp_sendstr_chunk(req, myStationLine);

    if (!g_config.msg_enable || !g_config.msg_mycall[0]) {
        httpd_resp_sendstr_chunk(req, "<p class='msg-err'>" TR_MSGCHAT_DISABLED_NOTE "</p>");
    }

    // -- Chat panel: big scrolling list of received/sent messages, polled
    //    from /msgchat/list. --
    httpd_resp_sendstr_chunk(req, "<div id='msgChatBox' class='chat-box'><div class='chat-empty'>" TR_MSGCHAT_LOADING "</div></div>");

    // -- Compose row: destination callsign, message text (max length is the
    //    standard APRS message text length, APRS_MSG_TEXT_STD_MAX), Send. --
    char composeMax[16];
    snprintf(composeMax, sizeof(composeMax), "%d", APRS_MSG_TEXT_STD_MAX);

    httpd_resp_sendstr_chunk(req, "<div class='chat-compose'>"
                                  "<div class='row'>"
                                  "<div>"
                                  "<label>" TR_MSGCHAT_TO "</label>"
                                  "<input type='text' id='msgToInput' maxlength='9' placeholder='" TR_MSGCHAT_TO_PLACEHOLDER
                                  "' oninput=\"this.value=this.value.toUpperCase()\">"
                                  "</div>"
                                  "<div style='flex:3;'>"
                                  "<label>" TR_MSGCHAT_TEXT "</label>"
                                  "<input type='text' id='msgTextInput' maxlength='");
    httpd_resp_sendstr_chunk(req, composeMax);
    httpd_resp_sendstr_chunk(req, "' placeholder='" TR_MSGCHAT_TEXT_PLACEHOLDER "' oninput='msgChatUpdateCounter()' "
                                  "onkeydown='if(event.key===\"Enter\"){event.preventDefault();msgChatSend();}'>"
                                  "<div class='chat-counter' id='msgChatCounter'>0/");
    httpd_resp_sendstr_chunk(req, composeMax);
    httpd_resp_sendstr_chunk(req, "</div>"
                                  "</div>"
                                  "</div>"
                                  "<button type='button' onclick='msgChatSend()'>" TR_MSGCHAT_SEND "</button>"
                                  "<span id='msgChatStatus'></span>"
                                  "</div>");

    // -- Inline JS: poll the history, send on click/Enter. Mirrors the
    //    dashboard's trafficPoll()/esc() pattern (short-poll + reschedule in
    //    a .catch().then() so a fetch error doesn't kill the loop). --
    httpd_resp_sendstr_chunk(
        req,
        "<script>"
        "var MSG_MAX=" /* opened below with the numeric literal */);
    httpd_resp_sendstr_chunk(req, composeMax);
    httpd_resp_sendstr_chunk(
        req,
        ";"
        "function msgEsc(s){return (s==null?'':String(s)).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
        "function msgChatUpdateCounter(){"
        "var t=document.getElementById('msgTextInput').value;"
        "document.getElementById('msgChatCounter').textContent=t.length+'/'+MSG_MAX;"
        "}"
        "function msgChatFmtTime(ts){"
        "var d=new Date(ts*1000);"
        "function p(n){return (n<10?'0':'')+n;}"
        "return p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());"
        "}"
        "function msgChatRender(list){"
        "var box=document.getElementById('msgChatBox');"
        "if(!list||!list.length){box.innerHTML=\"<div class='chat-empty'>" TR_MSGCHAT_EMPTY
        "</div>\";return;}"
        "var nearBottom=(box.scrollTop+box.clientHeight)>=(box.scrollHeight-40);"
        "var html='';"
        "for(var i=0;i<list.length;i++){"
        "var m=list[i];"
        "var cls='chat-bubble '+(m.dir==='rx'?'rx':'tx')+(m.status==='pending'?' pending':'');"
        "var who=m.dir==='rx'?msgEsc(m.call):'" TR_MSGCHAT_YOU "';"
        "html+=\"<div class='\"+cls+\"'><span class='chat-meta'>\"+who+' &middot; '+msgChatFmtTime(m.time)+\"</span>\"+msgEsc(m.text)+'</div>';"
        "}"
        "box.innerHTML=html;"
        "if(nearBottom)box.scrollTop=box.scrollHeight;"
        "}"
        "function msgChatPoll(){"
        "fetch('/msgchat/list').then(function(r){return r.json();}).then(function(d){"
        "msgChatRender(d);"
        "}).catch(function(){}).then(function(){setTimeout(msgChatPoll,3000);});"
        "}"
        "function msgChatSend(){"
        "var to=document.getElementById('msgToInput').value.trim();"
        "var text=document.getElementById('msgTextInput').value.trim();"
        "var status=document.getElementById('msgChatStatus');"
        "if(!to||!text){status.className='msg-err';status.textContent='" TR_MSGCHAT_ERR_EMPTY
        "';return;}"
        "status.className='';status.textContent='';"
        "fetch('/msgchat',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'msgTo='+encodeURIComponent(to)+'&msgText='+encodeURIComponent(text)})"
        ".then(function(r){return r.json();}).then(function(d){"
        "if(d&&d.ok){"
        "document.getElementById('msgTextInput').value='';"
        "msgChatUpdateCounter();"
        "status.className='msg-ok';status.textContent='" TR_MSGCHAT_SENT_OK
        "';"
        "msgChatPollOnce();"
        "}else{"
        "status.className='msg-err';status.textContent=(d&&d.error)?d.error:'" TR_MSGCHAT_SENT_FAIL
        "';"
        "}"
        "}).catch(function(){status.className='msg-err';status.textContent='" TR_MSGCHAT_SENT_FAIL
        "';});"
        "}"
        "function msgChatPollOnce(){"
        "fetch('/msgchat/list').then(function(r){return r.json();}).then(msgChatRender).catch(function(){});"
        "}"
        "msgChatUpdateCounter();msgChatPoll();"
        "</script>");

    web_send_footer(req);
    return ESP_OK;
}

// POST /msgchat -> send one APRS message (msgTo, msgText) and append it to
// the queue as an outbound entry. Responds with a small JSON status object
// instead of the usual "saved, redirecting" page, since this is polled/
// driven from JS rather than a normal form submit.
//
// Channel selection: sendAPRSMessage() already transmits over every channel
// enabled on the Message config page (g_config.msg_rf / g_config.msg_inet -
// "Send/Receive via RF" / "...via Internet"), i.e. every channel the
// operator has made available to messaging. This handler does not add a
// separate per-message channel choice on top of that - sending "via all
// available channels" is exactly what the existing Message-page
// configuration already does.
esp_err_t page_msgchat_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char body[400];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    char dest[16] = { 0 };
    char text[APRS_MSG_TEXT_STD_MAX + 1] = { 0 };
    web_form_get(body, "msgTo", dest, sizeof(dest));
    web_form_get(body, "msgText", text, sizeof(text));
    // Defensive clamp: the client's <input maxlength> already caps this, but
    // a hand-crafted POST could send more - text[] is sized exactly to
    // APRS_MSG_TEXT_STD_MAX+1 so web_form_get() itself cannot overflow it,
    // this just documents the intent.
    text[APRS_MSG_TEXT_STD_MAX] = 0;

    const char *error = NULL;
    if (!g_config.msg_enable)
        error = TR_MSGCHAT_ERR_DISABLED;
    else if (!g_config.msg_mycall[0])
        error = TR_MSGCHAT_ERR_NO_MYCALL;
    else if (!dest[0] || !text[0])
        error = TR_MSGCHAT_ERR_EMPTY;

    char resp[300];
    if (error) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", error);
    } else {
        sendAPRSMessage(dest, text, g_config.msg_encrypt);
        snprintf(resp, sizeof(resp), "{\"ok\":true}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// GET /msgchat/list -> JSON array of the in-memory message queue (RX + TX,
// oldest first), polled by the chat panel above.
esp_err_t page_msgchat_list(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    const size_t json_size = 8192;
    char *json = malloc(json_size);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    size_t n = message_dump_json(json, json_size);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, (ssize_t)n);
    free(json);
    return ESP_OK;
}
