/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "nvs.h"
#include "esp_ota_service_resume.h"

#define NVS_NAMESPACE  "ota_resume"

/* NVS key-name limit in IDF is 15 chars + NUL. Keep the templates short
 * so any non-negative int8_t (max "127") still fits. */
#define KEY_URI_FMT      "u%d"
#define KEY_WRITTEN_FMT  "w%d"
#define KEY_INDEX_FMT    "i%d"
#define KEY_SIZE_FMT     "s%d"
#define KEY_HASH_FMT     "h%d"
#define KEY_HASH_VALID_FMT "v%d"

static const char *TAG = "esp_ota_svc_resume";

static void make_key(char *dst, size_t dst_sz, const char *fmt, int idx)
{
    snprintf(dst, dst_sz, fmt, idx);
}

esp_err_t esp_ota_service_resume_save(const esp_ota_service_resume_point_t *point)
{
    if (point == NULL) {
        ESP_LOGE(TAG, "Save failed: point is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (point->item_index < 0) {
        ESP_LOGE(TAG, "Save failed: item_index %d is negative", point->item_index);
        return ESP_ERR_INVALID_ARG;
    }
    /* URI buffer is fixed-size; caller must have guaranteed NUL-termination.
     * Reject anything that lost its terminator in the fixed-size field. */
    if (memchr(point->uri, '\0', sizeof(point->uri)) == NULL) {
        ESP_LOGE(TAG, "Save failed: URI not NUL-terminated within %zu bytes", sizeof(point->uri));
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_open returned %s", esp_err_to_name(ret));
        return ret;
    }

    char key[16];
    make_key(key, sizeof(key), KEY_URI_FMT, point->item_index);
    ret = nvs_set_str(handle, key, point->uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_set_str(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    make_key(key, sizeof(key), KEY_WRITTEN_FMT, point->item_index);
    ret = nvs_set_u32(handle, key, point->written_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_set_u32(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    make_key(key, sizeof(key), KEY_INDEX_FMT, point->item_index);
    ret = nvs_set_i8(handle, key, point->item_index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_set_i8(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    make_key(key, sizeof(key), KEY_SIZE_FMT, point->item_index);
    ret = nvs_set_u32(handle, key, point->image_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_set_u32(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    make_key(key, sizeof(key), KEY_HASH_VALID_FMT, point->item_index);
    ret = nvs_set_u8(handle, key, point->has_sha256 ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_set_u8(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }
    if (point->has_sha256) {
        make_key(key, sizeof(key), KEY_HASH_FMT, point->item_index);
        ret = nvs_set_blob(handle, key, point->sha256, sizeof(point->sha256));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Save failed: nvs_set_blob(%s) returned %s", key, esp_err_to_name(ret));
            goto done;
        }
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: nvs_commit returned %s", esp_err_to_name(ret));
    }

done:
    nvs_close(handle);
    return ret;
}

esp_err_t esp_ota_service_resume_load(int item_index, esp_ota_service_resume_point_t *point)
{
    if (point == NULL) {
        ESP_LOGE(TAG, "Load failed: point is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (item_index < 0) {
        ESP_LOGE(TAG, "Load failed: item_index %d is negative", item_index);
        return ESP_ERR_INVALID_ARG;
    }

    memset(point, 0, sizeof(*point));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Load failed: nvs_open returned %s", esp_err_to_name(ret));
        }
        return ret;
    }

    char key[16];
    size_t uri_len = sizeof(point->uri);
    make_key(key, sizeof(key), KEY_URI_FMT, item_index);
    ret = nvs_get_str(handle, key, point->uri, &uri_len);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Load: nvs_get_str(%s) returned %s", key, esp_err_to_name(ret));
        }
        goto done;
    }

    make_key(key, sizeof(key), KEY_WRITTEN_FMT, item_index);
    ret = nvs_get_u32(handle, key, &point->written_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load failed: nvs_get_u32(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    make_key(key, sizeof(key), KEY_INDEX_FMT, item_index);
    ret = nvs_get_i8(handle, key, &point->item_index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load failed: nvs_get_i8(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    /* Size/hash were added after the original URI-only resume record.  Treat
     * missing optional identity fields as a legacy record; callers that know
     * the expected identity will reject it before using the offset. */
    make_key(key, sizeof(key), KEY_SIZE_FMT, item_index);
    ret = nvs_get_u32(handle, key, &point->image_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        point->image_size = 0;
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load failed: nvs_get_u32(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }

    uint8_t has_sha256 = 0;
    make_key(key, sizeof(key), KEY_HASH_VALID_FMT, item_index);
    ret = nvs_get_u8(handle, key, &has_sha256);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load failed: nvs_get_u8(%s) returned %s", key, esp_err_to_name(ret));
        goto done;
    }
    point->has_sha256 = has_sha256 != 0;
    if (point->has_sha256) {
        size_t hash_len = sizeof(point->sha256);
        make_key(key, sizeof(key), KEY_HASH_FMT, item_index);
        ret = nvs_get_blob(handle, key, point->sha256, &hash_len);
        if (ret != ESP_OK || hash_len != sizeof(point->sha256)) {
            ESP_LOGW(TAG, "Load failed: incomplete SHA-256 identity for item %d", item_index);
            ret = ESP_ERR_NVS_NOT_FOUND;
            goto done;
        }
    }

    ESP_LOGI(TAG,
             "Resume point loaded [idx=%d]: written=%" PRIu32 " size=%" PRIu32
             " sha256=%d uri=%.64s...",
             point->item_index, point->written_bytes, point->image_size,
             point->has_sha256, point->uri);

done:
    nvs_close(handle);
    return ret;
}

esp_err_t esp_ota_service_resume_clear(int item_index)
{
    if (item_index < 0) {
        ESP_LOGE(TAG, "Clear failed: item_index %d is negative", item_index);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Clear failed: nvs_open returned %s", esp_err_to_name(ret));
        return ret;
    }

    /* Best-effort: erase_key returns NOT_FOUND if that specific key does not exist.
     * We tolerate NOT_FOUND on each of the three keys independently. */
    char key[16];
    const char *fmts[] = {KEY_URI_FMT, KEY_WRITTEN_FMT, KEY_INDEX_FMT,
                          KEY_SIZE_FMT, KEY_HASH_FMT, KEY_HASH_VALID_FMT};
    for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        make_key(key, sizeof(key), fmts[i], item_index);
        esp_err_t er = nvs_erase_key(handle, key);
        if (er != ESP_OK && er != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Clear: nvs_erase_key(%s) returned %s", key, esp_err_to_name(er));
            ret = er;
        }
    }

    esp_err_t cr = nvs_commit(handle);
    if (cr != ESP_OK) {
        ESP_LOGE(TAG, "Clear failed: nvs_commit returned %s", esp_err_to_name(cr));
        ret = cr;
    }

    nvs_close(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Resume record cleared [idx=%d]", item_index);
    }
    return ret;
}

esp_err_t esp_ota_service_resume_clear_all(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Clear_all failed: nvs_open returned %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clear_all failed: nvs_erase_all returned %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clear_all failed: nvs_commit returned %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "All resume records cleared");
    }
    nvs_close(handle);
    return ret;
}
