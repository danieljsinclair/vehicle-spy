#pragma once

// OtaUpdateServer.h - Vanilla C++ OTA update handling
// Extracted from ota_update.ino for host testability

#include <cstdint>
#include <string>
#include <array>
#include <functional>
#include <memory>

namespace esp32_firmware {

// Forward declaration — the full ITime definition is pulled in only where the
// member is dereferenced (the .cpp). Keeps OtaUpdateServer.h free of the ITime
// header chain.
struct ITime;

// OTA configuration
struct OtaConfig {
    static constexpr uint16_t HTTP_PORT = 80;
    static constexpr uint32_t UPLOAD_TIMEOUT_MS = 300000;  // 5 minutes
    static constexpr uint32_t VERIFY_YIELD_INTERVAL = 0x3FFF;  // WDT feed every 16KB
    static constexpr uint32_t VERIFY_CHUNK_SIZE = 512;
    static constexpr uint32_t REBOOT_FLUSH_COUNT = 10;
    static constexpr uint32_t REBOOT_FLUSH_DELAY_MS = 100;
    static constexpr uint32_t REBOOT_DELAY_MS = 100;

    // OTA update sizing + command. FLASH_MAX_SIZE is the sketch-space estimate
    // passed to IUpdate::begin (the Arduino adapter's Update accepts the real
    // ESP.getFreeSketchSpace() value; the vanilla host path uses this bound so
    // it carries no ESP headers). CMD_FLASH mirrors the Arduino U_FLASH command.
    static constexpr uint32_t FLASH_MAX_SIZE = 1024 * 1024;
    static constexpr int CMD_FLASH = 0;  // U_FLASH

    // HTTP response codes
    static constexpr int HTTP_OK = 200;
    static constexpr int HTTP_UNAUTHORIZED = 401;
    static constexpr int HTTP_FORBIDDEN = 403;
    static constexpr int HTTP_INSUFFICIENT_STORAGE = 507;
    static constexpr int HTTP_BAD_REQUEST = 400;

    static constexpr const char* OTA_SIG_HDR = "X-Firmware-Signature";
};

// OTA error codes
enum class OtaError {
    NONE,
    SODIUM_NOT_READY,
    BAD_SIGNATURE_HEADER,
    NO_SIGNATURE,
    UPDATE_BEGIN_FAILED,
    UPDATE_WRITE_FAILED,
    UPDATE_END_FAILED,
    UPDATE_ERROR,
    NO_OTA_PARTITION,
    SIGNATURE_VERIFY_FAILED,
    SET_BOOT_PARTITION_FAILED,
    UPLOAD_ABORTED,
    UPLOAD_TIMEOUT
};

// HTTP server interface
struct IHttpUpload;  // defined below — IHttpServer::upload() returns one by ptr.

struct IHttpServer {
    virtual void collectHeaders(const char** headers, size_t count) = 0;
    virtual void on(const char* uri, int method, std::function<void()> handler) = 0;
    virtual void on(const char* uri, int method,
                    std::function<void()> postHandler,
                    std::function<void()> uploadHandler) = 0;
    virtual void begin() = 0;
    virtual void handleClient() = 0;
    virtual void send(int code, const char* contentType, const char* content) = 0;
    virtual std::string header(const char* name) = 0;
    virtual void clientSetNoDelay(bool nodelay) = 0;
    virtual void clientFlush() = 0;
    virtual void clientStop() = 0;
    // Snapshot of the in-flight multipart upload the WebServer is currently
    // driving the upload-handler closure with. The Arduino WebServer owns the
    // upload and surfaces it via WebServer::upload(); this getter lets the
    // vanilla's upload-handler closure translate + forward it to handleUpload.
    // Returns nullptr when no upload is in progress.
    virtual std::unique_ptr<IHttpUpload> upload() = 0;
    virtual ~IHttpServer() = default;
};

// HTTP upload interface
struct IHttpUpload {
    enum Status {
        UPLOAD_FILE_START,
        UPLOAD_FILE_WRITE,
        UPLOAD_FILE_END,
        UPLOAD_FILE_ABORTED
    };

    Status status = UPLOAD_FILE_START;
    const char* name = nullptr;
    const char* filename = nullptr;
    uint8_t* buf = nullptr;
    size_t currentSize = 0;
    size_t totalSize = 0;
};

// Update interface (firmware update)
struct IUpdate {
    virtual bool begin(size_t size, int command) = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual bool end(bool evenIfError) = 0;
    virtual bool hasError() const = 0;
    virtual void abort() = 0;
    virtual ~IUpdate() = default;
};

// Opaque handle for an ESP-IDF partition. Forward-declared here and defined
// ONLY in the adapter (ArduinoPartition wraps esp_partition_t*). The vanilla
// never dereferences it — it threads the handle between getRunningPartition/
// getNextUpdatePartition and size/read/etc. This replaces a raw `const void*`
// (cpp:S5008) with a named type-safe handle while keeping the vanilla free of
// ESP-IDF headers.
struct OtaPartitionRef;

// Partition interface
struct IPartition {
    virtual const OtaPartitionRef* getRunningPartition() = 0;
    virtual const OtaPartitionRef* getNextUpdatePartition(const OtaPartitionRef* running) = 0;
    // Capacity of the given OTA partition in bytes (mirrors esp_partition_t.size).
    // verifyPartition rejects an image whose declared size exceeds this capacity,
    // matching the inline ota_update.ino guard `size > part->size`.
    virtual uint32_t size(const OtaPartitionRef* partition) = 0;
    virtual int read(const OtaPartitionRef* partition, uint32_t offset, void* data, size_t size) = 0;
    virtual int getStatePartition(const OtaPartitionRef* partition, int* state) = 0;
    virtual int setBootPartition(const OtaPartitionRef* partition) = 0;
    virtual int markAppValidCancelRollback() = 0;
    virtual ~IPartition() = default;
};

// Crypto interface. The Ed25519ph verification state is adapter-owned (gap 4a:
// ArduinoCrypto holds the crypto_sign_ed25519ph_state member; the mock scripts
// the return). The vanilla threads NO state between these calls — exactly one
// verify is in flight at a time — so init/update/finalVerify carry no state ptr.
struct ICrypto {
    virtual int sodiumInit() = 0;
    virtual int signEd25519phInit() = 0;
    virtual int signEd25519phUpdate(const uint8_t* data, size_t len) = 0;
    // pubKey is a sentinel: the vanilla carries no signing key. The real device
    // key is crypto-domain and supplied by the adapter (ArduinoCrypto sources it
    // from OtaPublicKey.h; mocks script the verify return). See gap 6.
    virtual int signEd25519phFinalVerify(const uint8_t* sig, const uint8_t* pubKey) = 0;
    virtual ~ICrypto() = default;
};

class OtaUpdateServer {
public:
    using ErrorCallback = std::function<void(OtaError error, const char* message)>;
    using SuccessCallback = std::function<void()>;

    OtaUpdateServer(IHttpServer& http,
                    IUpdate& update, IPartition& partition, ICrypto& crypto,
                    ITime& time);

    // Initialize OTA server
    void setup();

    // Service incoming OTA connections - call from main loop()
    void loop();

    // Mark current firmware as valid on boot
    void markValidOnBoot();

    // WebServer-callable HTTP handlers (public for host testability — the
    // Arduino WebServer drives these in production; tests drive them directly
    // with a fake IHttpUpload). handleUpload is invoked per multipart chunk
    // (START/WRITE/END/ABORTED); the WebServer owns the upload and supplies it.
    void handleGet();
    void handlePost();
    void handleUpload(IHttpUpload& upload);

    // Set callbacks
    void setErrorCallback(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void setSuccessCallback(SuccessCallback cb) { successCallback_ = std::move(cb); }

    // Testable pure functions
    static bool hexToByte(char c, uint8_t& v);
    static bool parseHexSig(const std::string& hex, uint8_t* out);
    static std::string errorMessage(OtaError err);
    static int httpCode(OtaError err);

private:
    IHttpServer& http_;
    IUpdate& update_;
    IPartition& partition_;
    ICrypto& crypto_;
    ITime& time_;

    bool sodiumReady_ = false;
    uint32_t uploadStartTime_ = 0;
    std::array<uint8_t, 64> otaSig_;
    bool otaHasSig_ = false;
    std::string otaErr_;

    ErrorCallback errorCallback_;
    SuccessCallback successCallback_;

    void setupHandlers();

    // Record an OTA error: stash the human message in otaErr_ (so the WRITE/END
    // guards see it and skip subsequent chunks) and fire the error callback if
    // one is registered. Centralises the START/WRITE/END/ABORTED failure shape.
    void reportError(OtaError err);

    // Testable: verify partition signature. `part` is the opaque adapter handle
    // (OtaPartitionRef); the vanilla threads it to IPartition::size/read.
    bool verifyPartition(const OtaPartitionRef* part, uint32_t size, const uint8_t* sig);
};

} // namespace esp32_firmware