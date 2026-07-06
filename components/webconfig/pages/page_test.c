#include "app_config.h"
#include "esp_system.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_test_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_TEST, NULL);

    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_TEST_CONFIG_SELFTEST "</legend>"
                                  "<p>" TR_TEST_CONFIG_SELFTEST_BODY "</p>");
    char buf[300];
    snprintf(buf, sizeof(buf),
             "<p>" TR_TEST_IGATE_ENABLED ": <b>%s</b> &nbsp; " TR_TEST_DIGI_ENABLED ": <b>%s</b> &nbsp; " TR_TEST_TRACKER_ENABLED
             ": <b>%s</b> &nbsp; " TR_TEST_WX_ENABLED ": <b>%s</b></p>",
             g_config.igate_en ? TR_TEST_YES : TR_TEST_NO, g_config.digi_en ? TR_TEST_YES : TR_TEST_NO, g_config.trk_en ? TR_TEST_YES : TR_TEST_NO,
             g_config.wx_en ? TR_TEST_YES : TR_TEST_NO);
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "</fieldset>");

    httpd_resp_sendstr_chunk(req, "<fieldset><legend>" TR_TEST_HARDWARE_TEST "</legend>"
                                  "<p>" TR_TEST_HARDWARE_TEST_BODY "</p></fieldset>"
                                  "<form method='POST' action='/system'><button class='secondary' type='submit' "
                                  "formaction='javascript:void(0)' onclick=\"location.href='/dashboard'\">" TR_BTN_BACK_DASH "</button></form>");

    web_send_footer(req);
    return ESP_OK;
}
