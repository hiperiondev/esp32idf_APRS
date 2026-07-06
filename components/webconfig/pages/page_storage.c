#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "pages.h"
#include "storage.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_storage_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_FILE_STORAGE, "storage");

    size_t used = 0, total = 0;
    storage_usage(&used, &total);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "<p><b>" TR_STORAGE_USAGE "</b> %u / %u bytes</p>", (unsigned)used, (unsigned)total);
    httpd_resp_sendstr_chunk(req, hdr);

    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/upload' enctype='multipart/form-data'>"
                                  "<label>" TR_STORAGE_UPLOAD_FILE "</label><input type='file' name='file'>"
                                  "<button type='submit'>" TR_F_UPLOAD "</button></form>"
                                  "<form method='POST' action='/format' onsubmit=\"return confirm('" TR_STORAGE_CONFIRM_FORMAT "');\">"
                                  "<button class='danger' type='submit'>" TR_STORAGE_FORMAT_BTN "</button></form>"
                                  "<table><tr><th>" TR_F_NAME "</th><th>" TR_STORAGE_SIZE_BYTES "</th><th>" TR_STORAGE_ACTIONS "</th></tr>");

    DIR *dir = opendir(STORAGE_BASE_PATH);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            char full[300];
            snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, ent->d_name);
            struct stat st;
            long size = (stat(full, &st) == 0) ? (long)st.st_size : -1;
            char row[1650];
            snprintf(row, sizeof(row),
                     "<tr><td>%s</td><td>%ld</td>"
                     "<td><a class='btn' href='/download?file=%s'>" TR_STORAGE_DOWNLOAD "</a> "
                     "<a class='btn danger' href='/delete?file=%s' onclick=\"return confirm('" TR_STORAGE_CONFIRM_DELETE_PREFIX "%s?');\">" TR_STORAGE_DELETE
                     "</a></td></tr>",
                     ent->d_name, size, ent->d_name, ent->d_name, ent->d_name);
            httpd_resp_sendstr_chunk(req, row);
        }
        closedir(dir);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_download(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char query[128], fname[100];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || !web_form_get(query, "file", fname, sizeof(fname))) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    char full[300];
    snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, fname);
    FILE *f = fopen(full, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    char cdisp[150];
    snprintf(cdisp, sizeof(cdisp), "attachment; filename=\"%s\"", fname);
    httpd_resp_set_hdr(req, "Content-Disposition", cdisp);

    char buf[512];
    size_t rd;
    while ((rd = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, rd) != ESP_OK)
            break;
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t page_delete(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char query[128], fname[100];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && web_form_get(query, "file", fname, sizeof(fname))) {
        char rel[110];
        snprintf(rel, sizeof(rel), "/%s", fname);
        storage_delete(rel);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/storage");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t page_format(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    storage_format();
    // storage_format unmounts nothing; config will be regenerated with defaults
    // on next boot (or immediately, if caller wants). Trigger it now:
    extern bool app_config_load(void);
    app_config_load();
    web_send_saved_redirect(req, "/storage");
    return ESP_OK;
}

// NOTE: True multipart/form-data parsing (boundary parsing + streaming to file)
// is planned for a follow-up pass alongside /about firmware-OTA upload, since both
// share the same multipart reader. For now this endpoint acknowledges the request
// so the button doesn't dead-end, without silently pretending to store the file.
esp_err_t page_upload(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    // Drain body so the connection doesn't hang.
    char buf[512];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining > (int)sizeof(buf) ? sizeof(buf) : remaining);
        if (r <= 0)
            break;
        remaining -= r;
    }
    web_send_header(req, TR_F_UPLOAD, "storage");
    httpd_resp_sendstr_chunk(req, "<p class='msg-err'>" TR_STORAGE_UPLOAD_NOT_WIRED "</p><a class='btn' href='/storage'>" TR_STORAGE_BACK "</a>");
    web_send_footer(req);
    return ESP_OK;
}
