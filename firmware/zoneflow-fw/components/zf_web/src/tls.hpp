// SPDX-License-Identifier: Apache-2.0
// Self-signed TLS certificate for the optional HTTPS server.
#pragma once

#include <string>

#include "zf/config/config_store.hpp"

namespace zf::web {

struct TlsIdentity {
    std::string keyPem;
    std::string certPem;
    std::string fingerprint; // SHA-256 of the DER certificate, hex
};

// Loads the key and certificate from `store`, or creates them (EC P-256,
// valid 2025-01-01 to 2049-12-31, CN and SAN "<hostname>.local" and
// "<hostname>") and stores them. False if creation failed.
bool loadOrCreateTls(IBlobStorage &store, const std::string &hostname, TlsIdentity &out);

} // namespace zf::web
