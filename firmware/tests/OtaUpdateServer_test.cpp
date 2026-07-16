// OtaUpdateServer_test.cpp - Tests for OtaUpdateServer vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/OtaUpdateServer.h"
#include "vanilla/DiscoveryManager.h"  // ITime full definition for the upload-timeout seam
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

// Controllable ITime for the upload-timeout seam. millis() returns the
// scripted value so a WRITE can be driven past UPLOAD_TIMEOUT_MS deterministically.
class FakeTime : public ITime {
public:
    uint64_t getCurrentTimestamp() const override { return 0; }
    MOCK_METHOD(uint32_t, millis, (), (const, override));
};

// Mock HTTP Server interface
class MockHttpServer : public IHttpServer {
public:
    MOCK_METHOD(void, collectHeaders, (const char** headers, size_t count), (override));
    MOCK_METHOD(void, on, (const char* uri, int method, std::function<void()> handler), (override));
    MOCK_METHOD(void, on, (const char* uri, int method, std::function<void()> postHandler,
                          std::function<void()> uploadHandler), (override));
    MOCK_METHOD(void, begin, (), (override));
    MOCK_METHOD(void, handleClient, (), (override));
    MOCK_METHOD(void, send, (int code, const char* contentType, const char* content), (override));
    MOCK_METHOD(std::string, header, (const char* name), (override));
    MOCK_METHOD(void, clientSetNoDelay, (bool nodelay), (override));
    MOCK_METHOD(void, clientFlush, (), (override));
    MOCK_METHOD(void, clientStop, (), (override));
    MOCK_METHOD(std::unique_ptr<IHttpUpload>, upload, (), (override));

    std::string lastHeader;
    int lastSendCode{0};
    std::string lastSendContent;
    std::string lastSendContentType;
    bool noDelaySet{false};
    int flushCount{0};

    void reset() {
        lastHeader.clear();
        lastSendCode = 0;
        lastSendContent.clear();
        lastSendContentType.clear();
        noDelaySet = false;
        flushCount = 0;
    }
};

// Mock Update interface
class MockUpdate : public IUpdate {
public:
    MOCK_METHOD(bool, begin, (size_t size, int command), (override));
    MOCK_METHOD(size_t, write, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(bool, end, (bool evenIfError), (override));
    MOCK_METHOD(bool, hasError, (), (const, override));
    MOCK_METHOD(void, abort, (), (override));

    bool hasError_{false};
    size_t totalWritten{0};

    void reset() {
        hasError_ = false;
        totalWritten = 0;
    }
};

// Mock Partition interface. OtaPartitionRef is the opaque handle the vanilla
// threads (defined only in the Arduino adapter); for host tests we treat it as
// an opaque pointer and never dereference — the tests just thread sentinel
// pointers through and script size()/read() returns.
class MockPartition : public IPartition {
public:
    MOCK_METHOD(const OtaPartitionRef*, getRunningPartition, (), (override));
    MOCK_METHOD(const OtaPartitionRef*, getNextUpdatePartition, (const OtaPartitionRef* running), (override));
    MOCK_METHOD(uint32_t, size, (const OtaPartitionRef* partition), (override));
    MOCK_METHOD(int, read, (const OtaPartitionRef* partition, uint32_t offset, void* data, size_t size), (override));
    MOCK_METHOD(int, getStatePartition, (const OtaPartitionRef* partition, int* state), (override));
    MOCK_METHOD(int, setBootPartition, (const OtaPartitionRef* partition), (override));
    MOCK_METHOD(int, markAppValidCancelRollback, (), (override));

    bool runningPartitionValid{true};
    bool nextStatePendingVerify{false};
    bool readSuccess{true};

    void reset() {
        runningPartitionValid = true;
        nextStatePendingVerify = false;
        readSuccess = true;
    }
};

// Mock Crypto interface
class MockCrypto : public ICrypto {
public:
    MOCK_METHOD(int, sodiumInit, (), (override));
    MOCK_METHOD(int, signEd25519phInit, (), (override));
    MOCK_METHOD(int, signEd25519phUpdate, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(int, signEd25519phFinalVerify, (const uint8_t* sig, const uint8_t* pubKey), (override));

    int sodiumInitResult{0};
    bool sigValid{true};

    void reset() {
        sodiumInitResult = 0;
        sigValid = true;
    }
};

class OtaUpdateServerTest : public ::testing::Test {
protected:
    MockHttpServer httpMock;
    MockUpdate updateMock;
    MockPartition partitionMock;
    MockCrypto cryptoMock;
    FakeTime timeMock;
    std::unique_ptr<OtaUpdateServer> otaServer;

    void SetUp() override {
        httpMock.reset();
        updateMock.reset();
        partitionMock.reset();
        cryptoMock.reset();
        arduino_mock::resetAllMocks();
        // Default: the clock is stopped at t=0. Tests that exercise the
        // upload-timeout override this with EXPECT_CALL sequences.
        ON_CALL(timeMock, millis()).WillByDefault(Return(0));
    }

    void TearDown() override {
        otaServer.reset();
    }
};

// Pure function tests
TEST_F(OtaUpdateServerTest, HexToByte_ValidDigits) {
    uint8_t v;
    EXPECT_TRUE(OtaUpdateServer::hexToByte('0', v));
    EXPECT_EQ(v, 0);
    EXPECT_TRUE(OtaUpdateServer::hexToByte('9', v));
    EXPECT_EQ(v, 9);
    EXPECT_TRUE(OtaUpdateServer::hexToByte('a', v));
    EXPECT_EQ(v, 10);
    EXPECT_TRUE(OtaUpdateServer::hexToByte('f', v));
    EXPECT_EQ(v, 15);
    EXPECT_TRUE(OtaUpdateServer::hexToByte('A', v));
    EXPECT_EQ(v, 10);
    EXPECT_TRUE(OtaUpdateServer::hexToByte('F', v));
    EXPECT_EQ(v, 15);
}

TEST_F(OtaUpdateServerTest, HexToByte_InvalidDigits) {
    uint8_t v;
    EXPECT_FALSE(OtaUpdateServer::hexToByte('g', v));
    EXPECT_FALSE(OtaUpdateServer::hexToByte('x', v));
    EXPECT_FALSE(OtaUpdateServer::hexToByte('@', v));
    EXPECT_FALSE(OtaUpdateServer::hexToByte(' ', v));
}

TEST_F(OtaUpdateServerTest, ParseHexSig_ValidHex) {
    std::string hexSig = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                         "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
    uint8_t out[64];

    EXPECT_TRUE(OtaUpdateServer::parseHexSig(hexSig, out));
    EXPECT_EQ(out[0], 0x01);
    EXPECT_EQ(out[1], 0x23);
    EXPECT_EQ(out[62], 0xCD);
    EXPECT_EQ(out[63], 0xEF);
}

TEST_F(OtaUpdateServerTest, ParseHexSig_InvalidLength) {
    std::string shortSig = "0123";
    uint8_t out[64];

    EXPECT_FALSE(OtaUpdateServer::parseHexSig(shortSig, out));
}

TEST_F(OtaUpdateServerTest, ParseHexSig_InvalidCharacters) {
    std::string badSig = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
                        "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
    uint8_t out[64];

    EXPECT_FALSE(OtaUpdateServer::parseHexSig(badSig, out));
}

TEST_F(OtaUpdateServerTest, ErrorMessage_AllErrorCodes) {
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::NONE), "unknown error");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::SODIUM_NOT_READY), "sodium library not ready");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::BAD_SIGNATURE_HEADER), "invalid signature header format");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::NO_SIGNATURE), "missing signature header");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPDATE_BEGIN_FAILED), "failed to begin update");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPDATE_WRITE_FAILED), "firmware write failed");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPDATE_END_FAILED), "failed to end update");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPDATE_ERROR), "update error");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::NO_OTA_PARTITION), "no OTA partition available");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::SIGNATURE_VERIFY_FAILED), "signature verification failed");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::SET_BOOT_PARTITION_FAILED), "failed to set boot partition");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPLOAD_ABORTED), "upload aborted");
    EXPECT_EQ(OtaUpdateServer::errorMessage(OtaError::UPLOAD_TIMEOUT), "upload timeout");
}

TEST_F(OtaUpdateServerTest, HttpCode_Mapping) {
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::NONE), 200);
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::SIGNATURE_VERIFY_FAILED), 403);
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::BAD_SIGNATURE_HEADER), 403);
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::UPLOAD_TIMEOUT), 400);
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::NO_SIGNATURE), 401);
    EXPECT_EQ(OtaUpdateServer::httpCode(OtaError::UPDATE_BEGIN_FAILED), 400);
}

// Setup tests
TEST_F(OtaUpdateServerTest, Setup_InitializesComponents) {
    cryptoMock.sodiumInitResult = 0;

    EXPECT_CALL(cryptoMock, sodiumInit()).WillOnce(Return(0));
    EXPECT_CALL(httpMock, collectHeaders(_, _));
    EXPECT_CALL(httpMock, on(_, _, _));  // GET handler
    EXPECT_CALL(httpMock, on(_, _, _, _));  // POST handler with upload
    EXPECT_CALL(httpMock, begin());

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock
    );
    otaServer->setup();
}

TEST_F(OtaUpdateServerTest, Setup_SodiumInitFails_DoesNotCrash) {
    cryptoMock.sodiumInitResult = -1;

    EXPECT_CALL(cryptoMock, sodiumInit()).WillOnce(Return(-1));
    EXPECT_CALL(httpMock, collectHeaders(_, _));
    EXPECT_CALL(httpMock, on(_, _, _));  // GET handler
    EXPECT_CALL(httpMock, on(_, _, _, _));  // POST handler with upload
    EXPECT_CALL(httpMock, begin());

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock
    );
    otaServer->setup();
}

// Mark valid on boot tests
TEST_F(OtaUpdateServerTest, MarkValidOnBoot_NoRunningPartition_DoesNothing) {
    partitionMock.runningPartitionValid = false;

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(0);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock
    );
    otaServer->markValidOnBoot();
}

TEST_F(OtaUpdateServerTest, MarkValidOnBoot_PendingVerify_MarksValid) {
    // Opaque sentinel handle (never dereferenced — the mock scripts the return).
    const OtaPartitionRef* dummyPartition = reinterpret_cast<const OtaPartitionRef*>(0x1000);

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(dummyPartition));
    EXPECT_CALL(partitionMock, getStatePartition(dummyPartition, _))
        .WillOnce([](const OtaPartitionRef*, int* state) {
            *state = 1;  // ESP_OTA_IMG_PENDING_VERIFY
            return 0;
        });
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(1);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock
    );
    otaServer->markValidOnBoot();
}

TEST_F(OtaUpdateServerTest, MarkValidOnBoot_NotPendingVerify_DoesNothing) {
    const OtaPartitionRef* dummyPartition = reinterpret_cast<const OtaPartitionRef*>(0x1000);

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(dummyPartition));
    EXPECT_CALL(partitionMock, getStatePartition(dummyPartition, _))
        .WillOnce([](const OtaPartitionRef*, int* state) {
            *state = 0;  // Not pending verify
            return 0;
        });
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(0);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock
    );
    otaServer->markValidOnBoot();
}

// Edge case: empty signature header
TEST_F(OtaUpdateServerTest, ParseHexSig_EmptyString) {
    std::string emptySig;
    uint8_t out[64];

    EXPECT_FALSE(OtaUpdateServer::parseHexSig(emptySig, out));
}

// Edge case: hex string with spaces
TEST_F(OtaUpdateServerTest, ParseHexSig_StringWithSpaces) {
    std::string spaceSig = "0123 4567 89AB CDEF 0123 4567 89AB CDEF"
                          "0123 4567 89AB CDEF 0123 4567 89AB CDEF"
                          "0123 4567 89AB CDEF 0123 4567 89AB CDEF"
                          "0123 4567 89AB CDEF 0123 4567 89AB CDEF";
    uint8_t out[64];

    EXPECT_FALSE(OtaUpdateServer::parseHexSig(spaceSig, out));
}

// ===========================================================================
// handleUpload state-machine tests (Stage 5 blind spec-first TDD).
//
// CONTRACT (from OtaUpdateServer.h + relayed spec; blind to OtaUpdateServer.cpp
// and ota_update.ino): handleUpload(IHttpUpload&) is the per-multipart-chunk
// handler (START -> WRITE* -> END, or ABORTED). It is VOID; outcomes are
// observed via (a) the errorCallback(OtaError, message) and (b) mock call
// expectations on IUpdate (begin/write/end/abort). Internal state (otaErr_,
// otaHasSig_, uploadStartTime_) is private and observed only indirectly.
//
// sodiumReady_ is driven via setup() (crypto_.sodiumInit() < 0 => false).
// These tests are RED until tech-arch extracts the real handleUpload logic
// from ota_update.ino and makes handleUpload(IHttpUpload&) public.
//
// SCOPE NOTE: the WRITE-timeout branch (elapsed > UPLOAD_TIMEOUT_MS = 300000ms)
// is NOT covered here — it needs a mockable clock and OtaUpdateServer has no
// ITime seam, so the 5-minute timeout can't be driven deterministically blind.
// Flagged for tech-arch as a coverage gap requiring a time seam.
// ===========================================================================

namespace {
// Captures the last (OtaError, message) delivered to the error callback so a
// test can assert the error TYPE/intent without asserting the exact string
// (per the exception policy: assert intent + key content, not word-for-word).
struct ErrorCapture {
    bool fired = false;
    OtaError error = OtaError::NONE;
    std::string message;
};

// A valid 128-hex-char (64-byte) signature string — parseHexSig accepts this.
// Content is arbitrary valid hex; these tests do not assert signature
// *correctness* (that's verifyPartition's job via the END path), only that
// START accepts a well-formed header and rejects a malformed/missing one.
const char* kValidSigHex =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// Build a stack IHttpUpload in the given state with a small payload buffer.
IHttpUpload makeUpload(IHttpUpload::Status status, const uint8_t* data,
                       size_t len) {
    IHttpUpload u;
    u.status = status;
    u.buf = const_cast<uint8_t*>(data);
    u.currentSize = len;
    u.totalSize = len;
    u.name = "firmware";
    u.filename = "fw.bin";
    return u;
}
} // namespace

// Helper: run setup() with the given sodium-init result so sodiumReady_ is
// deterministic, wire the error callback to the capture sink, and register
// permissive expectations for the setup() boilerplate (collectHeaders /
// http.on / http.begin). Each handleUpload test uses this to reach a known
// sodium state, then drives handleUpload directly.
void primeOta(MockHttpServer& httpMock,
              MockUpdate& updateMock, MockPartition& partitionMock,
              MockCrypto& cryptoMock, FakeTime& timeMock,
              std::unique_ptr<OtaUpdateServer>& otaServer,
              int sodiumInitResult, ErrorCapture& sink) {
    EXPECT_CALL(cryptoMock, sodiumInit()).WillOnce(Return(sodiumInitResult));
    EXPECT_CALL(httpMock, collectHeaders(_, _)).Times(AnyNumber());
    EXPECT_CALL(httpMock, on(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(httpMock, on(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(httpMock, begin()).Times(AnyNumber());

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updateMock, partitionMock, cryptoMock, timeMock);
    otaServer->setErrorCallback(
        [&sink](OtaError err, const char* msg) {
            sink.fired = true;
            sink.error = err;
            sink.message = msg ? msg : "";
        });
    otaServer->setup();
}

// ---------------------------------------------------------------------------
// Spec §1 — UPLOAD_FILE_START
// ---------------------------------------------------------------------------

// sodiumReady_ false (sodiumInit < 0) => SODIUM_NOT_READY via callback, and
// IUpdate::begin must NOT be called (no update started on unverified crypto).
TEST_F(OtaUpdateServerTest, HandleUpload_Start_SodiumNotReady_NoBegin) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/-1, sink);

    EXPECT_CALL(updateMock, begin(_, _)).Times(0);

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::SODIUM_NOT_READY);
}

// sodiumReady_ true but signature header MISSING => BAD_SIGNATURE_HEADER,
// and IUpdate::begin must NOT be called.
TEST_F(OtaUpdateServerTest, HandleUpload_Start_MissingSignature_NoBegin) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    // header() returns empty for the signature header => "missing".
    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string()));
    EXPECT_CALL(updateMock, begin(_, _)).Times(0);

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::BAD_SIGNATURE_HEADER);
}

// sodiumReady_ true, valid signature header, but IUpdate::begin returns false
// => UPDATE_BEGIN_FAILED.
TEST_F(OtaUpdateServerTest, HandleUpload_Start_BeginFails_UpdateBeginFailed) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string(kValidSigHex)));
    EXPECT_CALL(updateMock, begin(_, _)).WillOnce(Return(false));

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPDATE_BEGIN_FAILED);
}

// Happy START: sodiumReady_, valid signature, begin succeeds => no error
// callback fires (sink.fired stays false). This is the precondition for all
// WRITE/END success-path tests.
TEST_F(OtaUpdateServerTest, HandleUpload_Start_ValidSigAndBegin_NoError) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string(kValidSigHex)));
    EXPECT_CALL(updateMock, begin(_, _)).WillOnce(Return(true));

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);

    EXPECT_FALSE(sink.fired) << "a successful START must not raise an error";
}

// Drive a successful START (sodiumReady + valid sig + begin ok) so otaErr_ is
// empty and otaHasSig_ is true entering the WRITE/END phase. Assumes primeOta
// has already run. After this, the caller sets WRITE/END expectations + upload.
void successfulStart(MockHttpServer& httpMock, MockUpdate& updateMock,
                     std::unique_ptr<OtaUpdateServer>& otaServer) {
    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string(kValidSigHex)));
    EXPECT_CALL(updateMock, begin(_, _)).WillOnce(Return(true));
    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
}

// ---------------------------------------------------------------------------
// Spec §2 — UPLOAD_FILE_WRITE (otaErr_ empty)
// ---------------------------------------------------------------------------

// WRITE success: IUpdate::write returns currentSize (all bytes accepted) =>
// no error callback. Pins the happy write path.
TEST_F(OtaUpdateServerTest, HandleUpload_Write_AllBytesAccepted_NoError) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
    EXPECT_CALL(updateMock, write(_, sizeof(payload)))
        .WillOnce(Return(sizeof(payload)));

    IHttpUpload wr = makeUpload(IHttpUpload::UPLOAD_FILE_WRITE, payload, sizeof(payload));
    otaServer->handleUpload(wr);

    EXPECT_FALSE(sink.fired) << "a successful WRITE must not raise an error";
}

// WRITE failure: IUpdate::write returns fewer than currentSize => UPDATE_WRITE_FAILED.
TEST_F(OtaUpdateServerTest, HandleUpload_Write_PartialWrite_UpdateWriteFailed) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
    // write accepts only 1 of 4 bytes => failure.
    EXPECT_CALL(updateMock, write(_, sizeof(payload))).WillOnce(Return(1));

    IHttpUpload wr = makeUpload(IHttpUpload::UPLOAD_FILE_WRITE, payload, sizeof(payload));
    otaServer->handleUpload(wr);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPDATE_WRITE_FAILED);
}

// WRITE after a FAILED START is a no-op: because otaErr_ is non-empty from the
// failed START, the WRITE path must not call IUpdate::write at all. This pins
// the "error state is sticky; later chunks are ignored" contract (observing
// private otaErr_ indirectly via behavior).
TEST_F(OtaUpdateServerTest, HandleUpload_Write_AfterFailedStart_NoWrite) {
    ErrorCapture sink;
    // sodiumInit < 0 => START will set otaErr_ = SODIUM_NOT_READY.
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/-1, sink);

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
    ASSERT_TRUE(sink.fired) << "precondition: START must have failed";

    // A subsequent WRITE must not touch IUpdate.
    EXPECT_CALL(updateMock, write(_, _)).Times(0);
    uint8_t payload[] = {0x01};
    IHttpUpload wr = makeUpload(IHttpUpload::UPLOAD_FILE_WRITE, payload, sizeof(payload));
    otaServer->handleUpload(wr);
}

// WRITE after the UPLOAD_TIMEOUT_MS window has elapsed => the stalled upload is
// aborted (IUpdate::abort) and UPLOAD_TIMEOUT reported; IUpdate::write is NOT
// called. Mirrors the inline ota_update.ino timeout guard this seam restores.
TEST_F(OtaUpdateServerTest, HandleUpload_Write_PastUploadTimeout_AbortsAndReports) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);  // START stamps uploadStartTime_ = millis() = 0

    // Advance the clock past the 5-minute upload window.
    EXPECT_CALL(timeMock, millis())
        .WillOnce(Return(OtaConfig::UPLOAD_TIMEOUT_MS + 1));

    // The timed-out WRITE must abort and report, never call write().
    EXPECT_CALL(updateMock, abort()).Times(1);
    EXPECT_CALL(updateMock, write(_, _)).Times(0);

    uint8_t payload[] = {0xde, 0xad};
    IHttpUpload wr = makeUpload(IHttpUpload::UPLOAD_FILE_WRITE, payload, sizeof(payload));
    otaServer->handleUpload(wr);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPLOAD_TIMEOUT);
}

// ---------------------------------------------------------------------------
// Spec §4 — UPLOAD_FILE_ABORTED
// ---------------------------------------------------------------------------

// ABORTED => IUpdate::abort() called and UPLOAD_ABORTED reported, regardless of
// prior state.
TEST_F(OtaUpdateServerTest, HandleUpload_Aborted_CallsAbortAndReports) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    EXPECT_CALL(updateMock, abort()).Times(1);

    IHttpUpload ab = makeUpload(IHttpUpload::UPLOAD_FILE_ABORTED, nullptr, 0);
    otaServer->handleUpload(ab);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPLOAD_ABORTED);
}

// ---------------------------------------------------------------------------
// Spec §3 — UPLOAD_FILE_END (otaErr_ empty entering END)
// ---------------------------------------------------------------------------

// END after a successful START+begin: IUpdate::end(true) returns false =>
// UPDATE_END_FAILED.
TEST_F(OtaUpdateServerTest, HandleUpload_End_EndReturnsFalse_UpdateEndFailed) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(false));

    IHttpUpload end = makeUpload(IHttpUpload::UPLOAD_FILE_END, nullptr, 0);
    otaServer->handleUpload(end);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPDATE_END_FAILED);
}

// END: end(true) ok but IUpdate::hasError() true => UPDATE_ERROR.
TEST_F(OtaUpdateServerTest, HandleUpload_End_UpdateHasError_UpdateError) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(true));
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(true));

    IHttpUpload end = makeUpload(IHttpUpload::UPLOAD_FILE_END, nullptr, 0);
    otaServer->handleUpload(end);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPDATE_ERROR);
}

// END: no next OTA partition available => NO_OTA_PARTITION. (getNextUpdatePartition
// returns null; verifyPartition never reached because part is null.)
TEST_F(OtaUpdateServerTest, HandleUpload_End_NoNextPartition_NoOtaPartition) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(true));
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(false));
    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, getNextUpdatePartition(nullptr))
        .WillOnce(Return(nullptr));

    IHttpUpload end = makeUpload(IHttpUpload::UPLOAD_FILE_END, nullptr, 0);
    otaServer->handleUpload(end);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::NO_OTA_PARTITION);
}

// END happy path: end(true) ok, no error, partition available, signature
// verifies, setBootPartition ok => no error callback. This is the full
// success chain through verifyPartition (Spec §8) reached indirectly via END.
TEST_F(OtaUpdateServerTest, HandleUpload_End_FullSuccess_NoError) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    const OtaPartitionRef* part = reinterpret_cast<const OtaPartitionRef*>(0x2000);
    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(true));
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(false));
    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, getNextUpdatePartition(nullptr)).WillOnce(Return(part));
    // verifyPartition capacity guard: totalSize (0) must be <= partition size.
    EXPECT_CALL(partitionMock, size(part)).WillOnce(Return(1024 * 1024));
    // verifyPartition happy path: crypto init/update/final-verify all succeed.
    EXPECT_CALL(cryptoMock, sodiumInit()).Times(AnyNumber()).WillRepeatedly(Return(0));
    EXPECT_CALL(cryptoMock, signEd25519phInit()).WillRepeatedly(Return(0));
    EXPECT_CALL(cryptoMock, signEd25519phUpdate(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(cryptoMock, signEd25519phFinalVerify(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(partitionMock, read(_, _, _, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(partitionMock, setBootPartition(part)).WillOnce(Return(0));

    IHttpUpload end = makeUpload(IHttpUpload::UPLOAD_FILE_END, nullptr, 0);
    otaServer->handleUpload(end);

    EXPECT_FALSE(sink.fired) << "a fully successful END must not raise an error";
}

// END: everything ok up to verifyPartition, but the signature does NOT verify
// (finalVerify nonzero) => SIGNATURE_VERIFY_FAILED and Update.abort() called.
// (Spec §8: final_verify nonzero => verifyPartition false => abort + fail.)
TEST_F(OtaUpdateServerTest, HandleUpload_End_SigVerifyFails_SignatureVerifyFailed) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    const OtaPartitionRef* part = reinterpret_cast<const OtaPartitionRef*>(0x2000);
    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(true));
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(false));
    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, getNextUpdatePartition(nullptr)).WillOnce(Return(part));
    // verifyPartition capacity guard: totalSize (0) must be <= partition size.
    EXPECT_CALL(partitionMock, size(part)).WillOnce(Return(1024 * 1024));
    EXPECT_CALL(cryptoMock, sodiumInit()).Times(AnyNumber()).WillRepeatedly(Return(0));
    EXPECT_CALL(cryptoMock, signEd25519phInit()).WillRepeatedly(Return(0));
    EXPECT_CALL(cryptoMock, signEd25519phUpdate(_, _)).WillRepeatedly(Return(0));
    // Signature verification fails.
    EXPECT_CALL(cryptoMock, signEd25519phFinalVerify(_, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(partitionMock, read(_, _, _, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(updateMock, abort()).Times(1);

    IHttpUpload end = makeUpload(IHttpUpload::UPLOAD_FILE_END, nullptr, 0);
    otaServer->handleUpload(end);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::SIGNATURE_VERIFY_FAILED);
}

// END: image larger than the OTA partition capacity => SIGNATURE_VERIFY_FAILED
// (verifyPartition rejects it before any crypto work). Mirrors the inline
// ota_update.ino guard `size > part->size`: a firmware image that exceeds the
// partition's actual capacity (e.g. a 1.5MB image on a 1MB partition) must be
// rejected. The vanilla previously hardcoded a 1MB cap, which both mis-rejected
// legitimate larger images AND accepted over-cap ones on bigger partitions;
// IPartition::size() restores the inline's per-partition check exactly.
TEST_F(OtaUpdateServerTest, HandleUpload_End_ImageLargerThanPartition_SignatureVerifyFailed) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);
    successfulStart(httpMock, updateMock, otaServer);

    const OtaPartitionRef* part = reinterpret_cast<const OtaPartitionRef*>(0x2000);
    EXPECT_CALL(updateMock, end(true)).WillOnce(Return(true));
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(false));
    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, getNextUpdatePartition(nullptr)).WillOnce(Return(part));
    // Partition reports 1MB; the uploaded image is 1.5MB => over-capacity.
    constexpr uint32_t kPartitionSize = 1024u * 1024u;
    constexpr uint32_t kImageSize = kPartitionSize + (512u * 1024u);
    EXPECT_CALL(partitionMock, size(part)).WillOnce(Return(kPartitionSize));
    // Over-capacity => verifyPartition returns before any crypto/partition read.
    EXPECT_CALL(cryptoMock, signEd25519phInit()).Times(0);
    EXPECT_CALL(cryptoMock, signEd25519phUpdate(_, _)).Times(0);
    EXPECT_CALL(cryptoMock, signEd25519phFinalVerify(_, _)).Times(0);
    EXPECT_CALL(partitionMock, read(_, _, _, _)).Times(0);
    EXPECT_CALL(partitionMock, setBootPartition(_)).Times(0);
    EXPECT_CALL(updateMock, abort()).Times(1);

    // totalSize (not the chunk payload) is what verifyPartition checks; set it
    // directly past the capacity without a WRITE payload.
    IHttpUpload end;
    end.status = IHttpUpload::UPLOAD_FILE_END;
    end.totalSize = kImageSize;
    end.currentSize = 0;
    otaServer->handleUpload(end);

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::SIGNATURE_VERIFY_FAILED);
}

// ---------------------------------------------------------------------------
// Spec §5 — handlePost (post-upload HTTP response).
//
// handlePost is VOID and reads private otaErr_ (set by a prior handleUpload).
// So each §5 test first drives handleUpload into the otaErr state that matches
// the branch, then calls handlePost and asserts (a) the HTTP code sent and
// (b) the errorCallback OtaError. We assert the HTTP STATUS (intent), not the
// response body string; and the callback error TYPE, not its message.
//
// NOTE on substring coupling: §5 branches on whether otaErr_ CONTAINS
// "signature" / "sodium" (per spec). The otaErr_ strings come from
// errorMessage(OtaError); the drivers below pick states whose messages contain
// the relevant substring, so the test exercises handlePost's branch matching.
// ---------------------------------------------------------------------------

// §5a: otaErr contains "signature" (here via a START with a missing signature
// header => BAD_SIGNATURE_HEADER, message "invalid signature header format")
// => HTTP_FORBIDDEN + SIGNATURE_VERIFY_FAILED callback.
TEST_F(OtaUpdateServerTest, HandlePost_SignatureError_ForbiddenAndVerifyFailedCallback) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    // Drive START with no signature header => otaErr_ contains "signature".
    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string()));
    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
    ASSERT_TRUE(sink.fired) << "precondition: START must have set a signature error";

    // handlePost must now respond FORBIDDEN + fire SIGNATURE_VERIFY_FAILED.
    sink = ErrorCapture{};  // reset to observe handlePost's callback, not START's.
    EXPECT_CALL(httpMock, send(OtaConfig::HTTP_FORBIDDEN, _, _)).Times(1);
    otaServer->handlePost();

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::SIGNATURE_VERIFY_FAILED);
}

// §5b: otaErr contains "sodium" (here via sodiumInit < 0 => SODIUM_NOT_READY,
// message "sodium library not ready") => HTTP_FORBIDDEN + SODIUM_NOT_READY callback.
TEST_F(OtaUpdateServerTest, HandlePost_SodiumError_ForbiddenAndSodiumCallback) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/-1, sink);

    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
    ASSERT_TRUE(sink.fired) << "precondition: START must have set a sodium error";

    sink = ErrorCapture{};
    EXPECT_CALL(httpMock, send(OtaConfig::HTTP_FORBIDDEN, _, _)).Times(1);
    otaServer->handlePost();

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::SODIUM_NOT_READY);
}

// §5c: otaErr other (non-signature, non-sodium; here via UPDATE_BEGIN_FAILED,
// message "failed to begin update") => HTTP_BAD_REQUEST + UPDATE_ERROR callback.
TEST_F(OtaUpdateServerTest, HandlePost_OtherError_BadRequestAndUpdateErrorCallback) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    // START with valid sig but begin fails => otaErr_ = "failed to begin update".
    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string(kValidSigHex)));
    EXPECT_CALL(updateMock, begin(_, _)).WillOnce(Return(false));
    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
    ASSERT_TRUE(sink.fired) << "precondition: START must have set a begin error";

    sink = ErrorCapture{};
    EXPECT_CALL(httpMock, send(OtaConfig::HTTP_BAD_REQUEST, _, _)).Times(1);
    otaServer->handlePost();

    EXPECT_TRUE(sink.fired);
    EXPECT_EQ(sink.error, OtaError::UPDATE_ERROR);
}

// §5e: no otaErr + no Update error => HTTP_OK success path: successCallback
// fires, and the client is primed for reboot (clientSetNoDelay + Flush + Stop).
TEST_F(OtaUpdateServerTest, HandlePost_NoErrorNoUpdateError_OkAndSuccessCallback) {
    ErrorCapture errSink;
    bool successFired = false;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, errSink);
    otaServer->setSuccessCallback([&successFired]() { successFired = true; });

    // Successful START => otaErr_ stays empty.
    ON_CALL(httpMock, header(_)).WillByDefault(Return(std::string(kValidSigHex)));
    EXPECT_CALL(updateMock, begin(_, _)).WillOnce(Return(true));
    IHttpUpload start = makeUpload(IHttpUpload::UPLOAD_FILE_START, nullptr, 0);
    otaServer->handleUpload(start);
    ASSERT_FALSE(errSink.fired) << "precondition: START must have succeeded";

    // handlePost: no update error => HTTP_OK + success path. The reboot flush
    // is a bounded loop (OtaConfig::REBOOT_FLUSH_COUNT iterations) to drain the
    // response before reset, so clientFlush is expected that many times — not
    // once. clientSetNoDelay + clientStop are single ops.
    EXPECT_CALL(updateMock, hasError()).WillOnce(Return(false));
    EXPECT_CALL(httpMock, send(OtaConfig::HTTP_OK, _, _)).Times(1);
    EXPECT_CALL(httpMock, clientSetNoDelay(_)).Times(1);
    EXPECT_CALL(httpMock, clientFlush()).Times(OtaConfig::REBOOT_FLUSH_COUNT);
    EXPECT_CALL(httpMock, clientStop()).Times(1);
    otaServer->handlePost();

    EXPECT_TRUE(successFired) << "success callback must fire on the OK path";
    EXPECT_FALSE(errSink.fired) << "no error callback on the OK path";
}

// ---------------------------------------------------------------------------
// Spec §6 — handleGet (GET upload form).
// ---------------------------------------------------------------------------

// handleGet responds HTTP_OK with a text/html body (the upload form). Asserts
// the status code + content type only — NOT the HTML body (UI string, not a
// contract pin; per the exception policy, assert intent not exact text).
TEST_F(OtaUpdateServerTest, HandleGet_Responds_OkHtml) {
    ErrorCapture sink;
    primeOta(httpMock, updateMock, partitionMock, cryptoMock, timeMock,
             otaServer, /*sodiumInitResult=*/0, sink);

    EXPECT_CALL(httpMock,
                send(OtaConfig::HTTP_OK, testing::StrEq("text/html"), _))
        .Times(1);

    otaServer->handleGet();
}
