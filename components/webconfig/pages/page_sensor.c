#include "app_config.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"
#include <stdio.h>

esp_err_t page_sensor_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_SENSORS, "sensor");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/sensor'>");

    for (int i = 0; i < SENSOR_NUMBER; i++) {
        sensor_info_t *s = &g_config.sensor[i];
        char legend[24];
        snprintf(legend, sizeof(legend), "Sensor Slot %d", i);
        web_fieldset_open(req, legend);

        char n[16];
        snprintf(n, sizeof(n), "senEn%d", i);
        web_field_checkbox(req, TR_F_ENABLE, n, s->enable);
        snprintf(n, sizeof(n), "senType%d", i);
        web_field_int(req, TR_F_TYPE_SEE_SENSOR_ID, n, s->type);
        snprintf(n, sizeof(n), "senPort%d", i);
        web_field_int(req, TR_F_PORT_SEE_PORT_ID, n, s->port);
        snprintf(n, sizeof(n), "senAddr%d", i);
        web_field_int(req, TR_F_ADDRESS_REGISTER, n, s->address);
        snprintf(n, sizeof(n), "senSR%d", i);
        web_field_int(req, TR_F_SAMPLE_RATE_S, n, s->samplerate);
        snprintf(n, sizeof(n), "senAR%d", i);
        web_field_int(req, TR_F_AVERAGE_RATE_S, n, s->averagerate);
        snprintf(n, sizeof(n), "senA%d", i);
        web_field_float(req, TR_F_EQUATION_A_AV_U00B2_BV_C_2, n, s->eqns[0], "0.0001");
        snprintf(n, sizeof(n), "senB%d", i);
        web_field_float(req, TR_F_EQUATION_B, n, s->eqns[1], "0.0001");
        snprintf(n, sizeof(n), "senC%d", i);
        web_field_float(req, TR_F_EQUATION_C, n, s->eqns[2], "0.0001");
        snprintf(n, sizeof(n), "senName%d", i);
        web_field_text(req, TR_F_NAME, n, s->parm, 14);
        snprintf(n, sizeof(n), "senUnit%d", i);
        web_field_text(req, TR_F_UNIT, n, s->unit, 9);
        web_fieldset_close(req);
    }
    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_sensor_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char *body = malloc(4000);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    if (web_read_body(req, body, 4000) < 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    for (int i = 0; i < SENSOR_NUMBER; i++) {
        sensor_info_t *s = &g_config.sensor[i];
        char n[16];
        snprintf(n, sizeof(n), "senEn%d", i);
        s->enable = web_form_get_bool(body, n);
        snprintf(n, sizeof(n), "senType%d", i);
        s->type = (uint16_t)web_form_get_int(body, n, s->type);
        snprintf(n, sizeof(n), "senPort%d", i);
        s->port = (uint8_t)web_form_get_int(body, n, s->port);
        snprintf(n, sizeof(n), "senAddr%d", i);
        s->address = (uint16_t)web_form_get_int(body, n, s->address);
        snprintf(n, sizeof(n), "senSR%d", i);
        s->samplerate = (uint16_t)web_form_get_int(body, n, s->samplerate);
        snprintf(n, sizeof(n), "senAR%d", i);
        s->averagerate = (uint16_t)web_form_get_int(body, n, s->averagerate);
        snprintf(n, sizeof(n), "senA%d", i);
        s->eqns[0] = web_form_get_float(body, n, s->eqns[0]);
        snprintf(n, sizeof(n), "senB%d", i);
        s->eqns[1] = web_form_get_float(body, n, s->eqns[1]);
        snprintf(n, sizeof(n), "senC%d", i);
        s->eqns[2] = web_form_get_float(body, n, s->eqns[2]);
        snprintf(n, sizeof(n), "senName%d", i);
        web_form_get(body, n, s->parm, sizeof(s->parm));
        snprintf(n, sizeof(n), "senUnit%d", i);
        web_form_get(body, n, s->unit, sizeof(s->unit));
    }

    free(body);
    app_config_save();
    web_send_saved_redirect(req, "/sensor");
    return ESP_OK;
}
