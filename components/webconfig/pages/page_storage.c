/**
 * @file page_storage.c
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
 * @brief Web admin "File storage" page: lists the LittleFS contents and
 * implements file download, upload (multipart), delete and whole partition
 * format.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "pages.h"
#include "storage.h"
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_storage";

// Filenames land in three different contexts on this page - HTML text, an
// href query value, and (for delete) a data-* attribute read back by a
// smidgen of JS - and each context has its own special characters. A name
// with a space, '&', '"', or similar used to silently mangle the generated
// markup (breaking the link/button for that row, without any obvious error)
// once it existed anywhere outside typical config-file names, which is
// exactly what user-uploaded files can look like. escape for display/attr,
// encode for the query string.
static void append_file_row(httpd_req_t *req, const char *name, long size) {
    char esc[512], enc[512];
    web_html_attr_escape(name, esc, sizeof(esc));
    web_urlencode(name, enc, sizeof(enc));

    // esc/enc can each be up to sizeof(esc)-1 long (worst case: every byte
    // of the name escapes to a multi-char entity/percent-triplet), and esc
    // appears twice + enc appears twice in the row below, so size the
    // buffer from their actual lengths rather than guessing a fixed cap.
    size_t need = strlen(esc) * 2 + strlen(enc) * 2 + 300;
    char *row = malloc(need);
    if (!row)
        return;
    snprintf(row, need,
             "<tr><td>%s</td><td>%ld</td>"
             "<td><a class='btn' href='/download?file=%s'>" TR_STORAGE_DOWNLOAD "</a> "
             "<a class='btn danger' href='/delete?file=%s' data-fname=\"%s\" "
             "onclick=\"return confirm('" TR_STORAGE_CONFIRM_DELETE_PREFIX "'+this.dataset.fname+'?');\">" TR_STORAGE_DELETE "</a></td></tr>",
             esc, size, enc, enc, esc);
    httpd_resp_sendstr_chunk(req, row);
    free(row);
}

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
            append_file_row(req, ent->d_name, size);
        }
        closedir(dir);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    web_send_footer(req);
    return ESP_OK;
}

// A `file` query value is only ever supposed to be one of the flat names
// this page itself listed - never a path. Reject anything with a separator
// or a leading dot so a hand-crafted request can't walk out of /storage.
static bool safe_flat_filename(const char *fname) {
    if (!fname || fname[0] == 0 || fname[0] == '.')
        return false;
    for (const char *p = fname; *p; p++) {
        if (*p == '/' || *p == '\\')
            return false;
    }
    return true;
}

esp_err_t page_download(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char query[128], fname[100];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || !web_form_get(query, "file", fname, sizeof(fname)) ||
        !safe_flat_filename(fname)) {
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
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && web_form_get(query, "file", fname, sizeof(fname)) &&
        safe_flat_filename(fname)) {
        char rel[110];
        snprintf(rel, sizeof(rel), "/%s", fname);
        if (!storage_delete(rel))
            ESP_LOGW(TAG, "delete failed for '%s'", fname);
    } else {
        ESP_LOGW(TAG, "delete request with missing/unsafe file param");
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

// ---------------------------------------------------------------- Upload

typedef struct {
    FILE *f;
    const char *raw_name; // -> caller's filename_out buffer; already parsed
                           // from Content-Disposition by the time cb runs
    char sanitized[100];
    bool opened;
    bool error;
    size_t total;
} upload_ctx_t;

static esp_err_t upload_write_cb(void *ctx_v, const uint8_t *data, size_t len) {
    upload_ctx_t *ctx = (upload_ctx_t *)ctx_v;
    if (ctx->error)
        return ESP_FAIL;

    if (!ctx->opened) {
        ctx->opened = true;
        web_sanitize_filename(ctx->raw_name, ctx->sanitized, sizeof(ctx->sanitized));
        char full[300];
        snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, ctx->sanitized);
        ctx->f = fopen(full, "wb");
        if (!ctx->f) {
            ESP_LOGE(TAG, "fopen('%s') failed", full);
            ctx->error = true;
            return ESP_FAIL;
        }
    }

    if (len > 0 && fwrite(data, 1, len, ctx->f) != len) {
        ESP_LOGE(TAG, "fwrite failed for '%s' (disk full?)", ctx->sanitized);
        ctx->error = true;
        return ESP_FAIL;
    }
    ctx->total += len;
    return ESP_OK;
}

esp_err_t page_upload(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;

    char raw_name[100] = { 0 };
    upload_ctx_t ctx = { .f = NULL, .raw_name = raw_name, .sanitized = { 0 }, .opened = false, .error = false, .total = 0 };

    esp_err_t perr = web_multipart_receive_file(req, upload_write_cb, &ctx, raw_name, sizeof(raw_name));
    if (ctx.f)
        fclose(ctx.f);

    bool ok = (perr == ESP_OK) && !ctx.error && ctx.opened && ctx.total > 0;
    if (!ok) {
        ESP_LOGW(TAG, "upload failed: parse=%s opened=%d total=%u", esp_err_to_name(perr), (int)ctx.opened, (unsigned)ctx.total);
        if (ctx.opened && ctx.sanitized[0]) {
            // Don't leave a truncated/empty file behind after a failed upload.
            char full[300];
            snprintf(full, sizeof(full), "%s/%s", STORAGE_BASE_PATH, ctx.sanitized);
            remove(full);
        }
    }

    web_send_header(req, TR_F_UPLOAD, "storage");
    if (ok) {
        char esc[220];
        web_html_attr_escape(ctx.sanitized, esc, sizeof(esc));
        size_t need = strlen(esc) + 128;
        char *msg = malloc(need);
        if (msg) {
            snprintf(msg, need, "<p class='msg-ok'>" TR_STORAGE_UPLOAD_OK " %s (%u bytes)</p><a class='btn' href='/storage'>" TR_STORAGE_BACK "</a>", esc,
                     (unsigned)ctx.total);
            httpd_resp_sendstr_chunk(req, msg);
            free(msg);
        }
    } else {
        const char *reason = (perr == ESP_ERR_NOT_FOUND) ? TR_STORAGE_NO_FILE_CHOSEN : TR_STORAGE_UPLOAD_FAILED;
        httpd_resp_sendstr_chunk(req, "<p class='msg-err'>");
        httpd_resp_sendstr_chunk(req, reason);
        httpd_resp_sendstr_chunk(req, "</p><a class='btn' href='/storage'>" TR_STORAGE_BACK "</a>");
    }
    web_send_footer(req);
    return ESP_OK;
}
