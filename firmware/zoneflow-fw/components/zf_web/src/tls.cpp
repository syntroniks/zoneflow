// SPDX-License-Identifier: Apache-2.0
#include "tls.hpp"

#include <array>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "zf_web/crypto.hpp"

namespace zf::web {
namespace {

constexpr const char *kTag = "zf.web.tls";
constexpr const char *kKeyKey = "tls_key";
constexpr const char *kKeyCert = "tls_cert";
constexpr const char *kKeyFp = "tls_fp";

struct Contexts {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    Contexts()
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_pk_init(&key);
        mbedtls_x509write_crt_init(&crt);
    }
    ~Contexts()
    {
        mbedtls_x509write_crt_free(&crt);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
};

bool create(const std::string &hostname, TlsIdentity &out)
{
    Contexts c;
    const char *pers = "zoneflow-tls";
    if (mbedtls_ctr_drbg_seed(&c.drbg, mbedtls_entropy_func, &c.entropy, reinterpret_cast<const unsigned char *>(pers),
                              std::strlen(pers)) != 0) {
        return false;
    }
    if (mbedtls_pk_setup(&c.key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(c.key), mbedtls_ctr_drbg_random, &c.drbg) != 0) {
        return false;
    }
    const std::string local = hostname + ".local";
    const std::string subject = "CN=" + local + ",O=ZoneFlow";
    std::array<unsigned char, 16> serial{};
    esp_fill_random(serial.data(), serial.size());
    serial[0] &= 0x7F; // positive INTEGER
    serial[0] |= 0x01; // and non-zero
    mbedtls_x509write_crt_set_version(&c.crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&c.crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&c.crt, &c.key);
    mbedtls_x509write_crt_set_issuer_key(&c.crt, &c.key);
    if (mbedtls_x509write_crt_set_subject_name(&c.crt, subject.c_str()) != 0 ||
        mbedtls_x509write_crt_set_issuer_name(&c.crt, subject.c_str()) != 0 ||
        mbedtls_x509write_crt_set_serial_raw(&c.crt, serial.data(), serial.size()) != 0 ||
        mbedtls_x509write_crt_set_validity(&c.crt, "20250101000000", "20491231235959") != 0 ||
        mbedtls_x509write_crt_set_basic_constraints(&c.crt, 0, -1) != 0) {
        return false;
    }
    mbedtls_x509_san_list san2{};
    san2.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    san2.node.san.unstructured_name.tag = MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME;
    san2.node.san.unstructured_name.p = reinterpret_cast<unsigned char *>(const_cast<char *>(hostname.c_str()));
    san2.node.san.unstructured_name.len = hostname.size();
    mbedtls_x509_san_list san1{};
    san1.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    san1.node.san.unstructured_name.tag = MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME;
    san1.node.san.unstructured_name.p = reinterpret_cast<unsigned char *>(const_cast<char *>(local.c_str()));
    san1.node.san.unstructured_name.len = local.size();
    san1.next = &san2;
    if (mbedtls_x509write_crt_set_subject_alternative_name(&c.crt, &san1) != 0) {
        return false;
    }

    std::array<unsigned char, 1600> buf{};
    const int der = mbedtls_x509write_crt_der(&c.crt, buf.data(), buf.size(), mbedtls_ctr_drbg_random, &c.drbg);
    if (der <= 0) {
        return false;
    }
    // DER is written at the end of the buffer.
    const Sha256Digest fp = Sha256::hash(buf.data() + buf.size() - der, static_cast<std::size_t>(der));
    out.fingerprint = toHex(fp.data(), fp.size());
    buf.fill(0);
    if (mbedtls_x509write_crt_pem(&c.crt, buf.data(), buf.size(), mbedtls_ctr_drbg_random, &c.drbg) != 0) {
        return false;
    }
    out.certPem = reinterpret_cast<const char *>(buf.data());
    buf.fill(0);
    if (mbedtls_pk_write_key_pem(&c.key, buf.data(), buf.size()) != 0) {
        return false;
    }
    out.keyPem = reinterpret_cast<const char *>(buf.data());
    buf.fill(0);
    return true;
}

} // namespace

bool loadOrCreateTls(IBlobStorage &store, const std::string &hostname, TlsIdentity &out)
{
    if (store.read(kKeyKey, out.keyPem) && store.read(kKeyCert, out.certPem) && store.read(kKeyFp, out.fingerprint) &&
        !out.keyPem.empty() && !out.certPem.empty()) {
        return true;
    }
    ESP_LOGI(kTag, "creating a self-signed certificate for %s.local", hostname.c_str());
    if (!create(hostname, out)) {
        ESP_LOGE(kTag, "certificate creation failed");
        return false;
    }
    if (!store.write(kKeyKey, out.keyPem) || !store.write(kKeyCert, out.certPem) ||
        !store.write(kKeyFp, out.fingerprint)) {
        ESP_LOGW(kTag, "certificate not stored; a new one is made at next boot");
    }
    ESP_LOGI(kTag, "certificate SHA-256 fingerprint %s", out.fingerprint.c_str());
    return true;
}

} // namespace zf::web
