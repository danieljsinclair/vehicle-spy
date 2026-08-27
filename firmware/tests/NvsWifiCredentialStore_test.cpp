// NvsWifiCredentialStore_test.cpp - Host tests for storeWifiCredentials
// Extracted from can-bridge.ino for host testability

#include "NvsWifiCredentialStore.h"

#include <gtest/gtest.h>
#include <string>

using esp32_firmware::INvsWifiStore;
using esp32_firmware::storeWifiCredentials;

namespace {

// FakeNvsStore: records all calls for test assertions. Tracks the active
// namespace so tests can verify the correct namespace is selected. Each
// putString returns a configurable byte count (default: value.size()).
class FakeNvsStore : public INvsWifiStore {
public:
    void begin(const char* name, bool readOnly) override {
        namespace_ = name ? name : "";
        readOnly_  = readOnly;
    }

    size_t putString(const char* key, const std::string& value) override {
        // Return configured bytesWritten if set, otherwise value.size().
        if (putStringResult_ >= 0) {
            auto r = static_cast<size_t>(putStringResult_);
            entries_.push_back({namespace_, key, value, r});
            return r;
        }
        entries_.push_back({namespace_, key, value, value.size()});
        return value.size();
    }

    void end() override {
        namespace_.clear();
        readOnly_ = false;
    }

    // Test helpers
    void reset() {
        entries_.clear();
        namespace_.clear();
        readOnly_ = false;
        putStringResult_ = -1;
    }

    // Override putString return value (-1 = use value.size() default).
    void setPutStringResult(int bytesWritten) { putStringResult_ = bytesWritten; }

    // Namespace set by the most recent begin() call (cleared by end()).
    const std::string& capturedNamespace() const { return namespace_; }
    bool wasReadOnly() const { return readOnly_; }

    struct Entry {
        std::string ns;
        std::string key;
        std::string value;
        size_t      bytesWritten;
    };
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::string              namespace_;
    bool                     readOnly_ = false;
    std::vector<Entry>       entries_;
    int                      putStringResult_ = -1;  // -1 = auto
};

} // anonymous namespace

// Happy path: valid SSID and password are written to the "wifi" namespace
// under the list-shaped schema keys (cred_count + indexed entry[0]).
// This test pins the on-disk key shape so phase-2 cannot silently change it.
TEST(NvsWifiCredentialStoreTest, WritesListSchemaKeysAndReturnsTrue) {
    FakeNvsStore store;
    bool ok = storeWifiCredentials(store, "my-ssid", "my-pass");

    ASSERT_TRUE(ok);
    ASSERT_FALSE(store.entries().empty());
    EXPECT_EQ(store.entries().front().ns, "wifi");

    // List-shaped schema: cred_count first, then indexed entry[0].
    ASSERT_EQ(store.entries().size(), 3);
    EXPECT_EQ(store.entries()[0].key,   "cred_count");
    EXPECT_EQ(store.entries()[0].value, "1");
    EXPECT_EQ(store.entries()[1].key,   "ssid_0");
    EXPECT_EQ(store.entries()[1].value, "my-ssid");
    EXPECT_EQ(store.entries()[2].key,   "pass_0");
    EXPECT_EQ(store.entries()[2].value, "my-pass");
}

// Edge case: strings with special characters are stored verbatim.
TEST(NvsWifiCredentialStoreTest, WritesSpecialCharactersVerbatim) {
    FakeNvsStore store;
    bool ok = storeWifiCredentials(store, "my-ssid!@#", "p@ss:word/123");

    ASSERT_TRUE(ok);
    ASSERT_EQ(store.entries().size(), 3);
    EXPECT_EQ(store.entries()[1].value, "my-ssid!@#");
    EXPECT_EQ(store.entries()[2].value, "p@ss:word/123");
}

// Edge case: if the first putString fails (returns 0), the remaining writes
// must NOT be called — partial writes leave the namespace in an inconsistent state.
TEST(NvsWifiCredentialStoreTest, RemainingWritesSkippedWhenFirstFails) {
    FakeNvsStore store;
    store.setPutStringResult(0);  // first putString returns 0 (failure)

    bool ok = storeWifiCredentials(store, "my-ssid", "my-pass");

    ASSERT_FALSE(ok);
    // Only the first write (cred_count) should have been attempted.
    ASSERT_EQ(store.entries().size(), 1);
    EXPECT_EQ(store.entries()[0].key, "cred_count");
}
