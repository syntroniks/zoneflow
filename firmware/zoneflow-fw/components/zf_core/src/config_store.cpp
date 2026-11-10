// SPDX-License-Identifier: Apache-2.0
#include "zf/config/config_store.hpp"

namespace zf {

const char *ConfigStore::toString(LoadStatus s)
{
    switch (s) {
    case LoadStatus::Loaded:
        return "loaded";
    case LoadStatus::Migrated:
        return "migrated";
    case LoadStatus::NewerSchema:
        return "newer_schema";
    case LoadStatus::Defaults:
        return "defaults";
    case LoadStatus::InvalidDefaults:
        return "invalid_defaults";
    }
    return "?";
}

ConfigStore::LoadResult ConfigStore::load()
{
    std::lock_guard<std::mutex> writer(writerMutex_);
    LoadResult result;
    std::string json;
    Config cfg = Config::defaults();
    if (!storage_.read(kConfigKey, json)) {
        result.status = LoadStatus::Defaults;
    } else {
        const ConfigParseResult parsed = configFromJson(json, cfg);
        result.validation = parsed.validation;
        if (!parsed.ok()) {
            // Keep the stored copy for diagnosis; a later update() replaces it.
            cfg = Config::defaults();
            result.status = LoadStatus::InvalidDefaults;
        } else if (parsed.origin == ConfigOrigin::Migrated) {
            result.status = LoadStatus::Migrated;
            storage_.write(kConfigKey, configToJson(cfg));
        } else if (parsed.origin == ConfigOrigin::Newer) {
            result.status = LoadStatus::NewerSchema;
        } else {
            result.status = LoadStatus::Loaded;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = cfg;
    }
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return result;
}

Config ConfigStore::get() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string ConfigStore::toJson(bool pretty) const { return configToJson(get(), pretty); }

ValidationResult ConfigStore::update(const Config &cfg)
{
    ValidationResult v = validate(cfg);
    if (!v.ok) {
        return v;
    }
    Config copy = cfg;
    copy.schemaVersion = kConfigSchemaVersion;
    const std::string json = configToJson(copy);
    std::lock_guard<std::mutex> writer(writerMutex_);
    if (json.empty() || !storage_.write(kConfigKey, json)) {
        return ValidationResult::bad("", "storage write failed");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = copy;
    }
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return ValidationResult::good();
}

ValidationResult ConfigStore::updateFromJson(std::string_view json)
{
    Config cfg;
    const ConfigParseResult parsed = configFromJson(json, cfg);
    if (!parsed.ok()) {
        return parsed.validation;
    }
    if (parsed.origin == ConfigOrigin::Newer) {
        // Writing it back as this firmware's schema would drop every field
        // this firmware does not know. A stored newer config is kept intact
        // for the same reason (see load()).
        return ValidationResult::bad("schema", "schema " + std::to_string(parsed.storedVersion) +
                                                   " is newer than this firmware supports (" +
                                                   std::to_string(kConfigSchemaVersion) + ")");
    }
    return update(cfg);
}

bool ConfigStore::factoryReset()
{
    std::lock_guard<std::mutex> writer(writerMutex_);
    const bool erased = storage_.erase(kConfigKey);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = Config::defaults();
    }
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return erased;
}

} // namespace zf
