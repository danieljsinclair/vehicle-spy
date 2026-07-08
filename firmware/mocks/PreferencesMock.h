#pragma once

// PreferencesMock.h - Mock Preferences (NVS) for host testing
// Implements IPreferences interface

#include <string>
#include <map>
#include <vector>

namespace esp32_firmware {

class PreferencesMock : public IPreferences {
public:
    PreferencesMock() = default;

    void begin(const char* name, bool readOnly) override {
        currentNamespace_ = name ? name : "";
        readOnly_ = readOnly;
    }

    void end() override {
        currentNamespace_.clear();
        readOnly_ = false;
    }

    size_t getBytesLength(const char* key) override {
        if (currentNamespace_.empty()) return 0;

        auto nsIt = storage_.find(currentNamespace_);
        if (nsIt == storage_.end()) return 0;

        auto keyIt = nsIt->second.find(key);
        if (keyIt == nsIt->second.end()) return 0;

        return keyIt->second.size();
    }

    std::string getString(const char* key, const std::string& defaultValue) override {
        if (currentNamespace_.empty()) return defaultValue;

        auto nsIt = storage_.find(currentNamespace_);
        if (nsIt == storage_.end()) return defaultValue;

        auto keyIt = nsIt->second.find(key);
        if (keyIt == nsIt->second.end()) return defaultValue;

        return keyIt->second;
    }

    size_t putString(const char* key, const std::string& value) override {
        if (currentNamespace_.empty() || readOnly_) return 0;

        storage_[currentNamespace_][key] = value;
        return value.size();
    }

    void clear() override {
        if (currentNamespace_.empty() || readOnly_) return;

        auto nsIt = storage_.find(currentNamespace_);
        if (nsIt != storage_.end()) {
            nsIt->second.clear();
        }
    }

    // Test helpers
    void setValue(const std::string& namespace_, const std::string& key, const std::string& value) {
        storage_[namespace_][key] = value;
    }

    std::string getValue(const std::string& namespace_, const std::string& key) const {
        auto nsIt = storage_.find(namespace_);
        if (nsIt == storage_.end()) return "";
        auto keyIt = nsIt->second.find(key);
        if (keyIt == nsIt->second.end()) return "";
        return keyIt->second;
    }

    bool hasKey(const std::string& namespace_, const std::string& key) const {
        auto nsIt = storage_.find(namespace_);
        if (nsIt == storage_.end()) return false;
        return nsIt->second.find(key) != nsIt->second.end();
    }

    void reset() {
        storage_.clear();
        currentNamespace_.clear();
        readOnly_ = false;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> storage_;
    std::string currentNamespace_;
    bool readOnly_ = false;
};

} // namespace esp32_firmware