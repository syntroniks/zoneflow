// SPDX-License-Identifier: Apache-2.0
// IBlobStorage over one NVS namespace, for modules that keep their own
// settings (zf_net identity, zf_web password and TLS key). NVS must already be
// initialised (zf_platform NvsBlobStorage::init does it in app_main).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "zf/config/config_store.hpp"

namespace zf::net {

class NvsNamespace final : public IBlobStorage {
public:
    // `ns` must outlive the object (a string literal). At most 15 characters.
    explicit NvsNamespace(const char *ns) : ns_(ns) {}
    esp_err_t open();
    bool read(const char *key, std::string &out) override;
    bool write(const char *key, std::string_view data) override;
    bool erase(const char *key) override;

private:
    const char *ns_;
    uint32_t handle_ = 0;
    bool open_ = false;
};

} // namespace zf::net
