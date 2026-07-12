#pragma once

// OtaUpdateServer.h - Vanilla C++ OTA update handling
// Extracted from ota_update.ino for host testability

#include <cstdint>
#include <string>
#include <array>
#include <functional>

namespace esp32_firmware {

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

// HTTP update server interface
struct IHttpUpdateServer {
    virtual void setup(IHttpServer* server, const char* path, const char* username, const char* password) = 0;
    virtual ~IHttpUpdateServer() = default;
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

// Partition interface
struct IPartition {
    virtual const void* getRunningPartition() = 0;
    virtual const void* getNextUpdatePartition(const void* running) = 0;
    virtual int read(const void* partition, uint32_t offset, void* data, size_t size) = 0;
    virtual int getStatePartition(const void* partition, int* state) = 0;
    virtual int setBootPartition(const void* partition) = 0;
    virtual int markAppValidCancelRollback() = 0;
    virtual ~IPartition() = default;
};

// Crypto interface
struct ICrypto {
    virtual int sodiumInit() = 0;
    virtual int signEd25519phInit(void* state) = 0;
    virtual int signEd25519phUpdate(void* state, const uint8_t* data, size_t len) = 0;
    virtual int signEd25519phFinalVerify(void* state, const uint8_t* sig, const uint8_t* pubKey) = 0;
    virtual ~ICrypto() = default;
};

// Public key (baked in)
extern const uint8_t OTA_PUBLIC_KEY[32];

class OtaUpdateServer {
public:
    using ErrorCallback = std::function<void(OtaError error, const char* message)>;
    using SuccessCallback = std::function<void()>;

    OtaUpdateServer(IHttpServer& http, IHttpUpdateServer& updater,
                    IUpdate& update, IPartition& partition, ICrypto& crypto);

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
    IHttpUpdateServer& updater_;
    IUpdate& update_;
    IPartition& partition_;
    ICrypto& crypto_;

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

    // Testable: verify partition signature
    bool verifyPartition(const void* part, uint32_t size, const uint8_t* sig);
};

} // namespace esp32_firmware