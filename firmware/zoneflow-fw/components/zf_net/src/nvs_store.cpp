// SPDX-License-Identifier: Apache-2.0
#include "zf_net/nvs_store.hpp"

#include "nvs.h"

namespace zf::net {

esp_err_t NvsNamespace::open()
{
    if (open_) {
        return ESP_OK;
    }
    nvs_handle_t h = 0;
    const esp_err_t err = nvs_open(ns_, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        handle_ = h;
        open_ = true;
    }
    return err;
}

bool NvsNamespace::read(const char *key, std::string &out)
{
    if (!open_) {
        return false;
    }
    size_t len = 0;
    if (nvs_get_blob(handle_, key, nullptr, &len) != ESP_OK) {
        return false;
    }
    out.resize(len);
    if (len > 0 && nvs_get_blob(handle_, key, out.data(), &len) != ESP_OK) {
        out.clear();
        return false;
    }
    return true;
}

bool NvsNamespace::write(const char *key, std::string_view data)
{
    if (!open_) {
        return false;
    }
    return nvs_set_blob(handle_, key, data.data(), data.size()) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}

bool NvsNamespace::erase(const char *key)
{
    if (!open_) {
        return false;
    }
    const esp_err_t err = nvs_erase_key(handle_, key);
    nvs_commit(handle_);
    return err == ESP_OK;
}

} // namespace zf::net
