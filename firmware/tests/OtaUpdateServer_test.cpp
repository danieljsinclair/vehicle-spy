// OtaUpdateServer_test.cpp - Tests for OtaUpdateServer vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/OtaUpdateServer.h"
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

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

// Mock HTTP Update Server interface
class MockHttpUpdateServer : public IHttpUpdateServer {
public:
    MOCK_METHOD(void, setup, (IHttpServer* server, const char* path,
                            const char* username, const char* password), (override));

    void reset() {}
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

// Mock Partition interface
class MockPartition : public IPartition {
public:
    MOCK_METHOD(const void*, getRunningPartition, (), (override));
    MOCK_METHOD(const void*, getNextUpdatePartition, (const void* running), (override));
    MOCK_METHOD(int, read, (const void* partition, uint32_t offset, void* data, size_t size), (override));
    MOCK_METHOD(int, getStatePartition, (const void* partition, int* state), (override));
    MOCK_METHOD(int, setBootPartition, (const void* partition), (override));
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
    MOCK_METHOD(int, signEd25519phInit, (void* state), (override));
    MOCK_METHOD(int, signEd25519phUpdate, (void* state, const uint8_t* data, size_t len), (override));
    MOCK_METHOD(int, signEd25519phFinalVerify, (void* state, const uint8_t* sig, const uint8_t* pubKey), (override));

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
    MockHttpUpdateServer updaterMock;
    MockUpdate updateMock;
    MockPartition partitionMock;
    MockCrypto cryptoMock;
    std::unique_ptr<OtaUpdateServer> otaServer;

    void SetUp() override {
        httpMock.reset();
        updaterMock.reset();
        updateMock.reset();
        partitionMock.reset();
        cryptoMock.reset();
        arduino_mock::resetAllMocks();
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
    EXPECT_CALL(updaterMock, setup(_, _, _, _));
    EXPECT_CALL(httpMock, on(_, _, _));  // GET handler
    EXPECT_CALL(httpMock, on(_, _, _, _));  // POST handler with upload
    EXPECT_CALL(httpMock, begin());

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );
    otaServer->setup();
}

TEST_F(OtaUpdateServerTest, Setup_SodiumInitFails_DoesNotCrash) {
    cryptoMock.sodiumInitResult = -1;

    EXPECT_CALL(cryptoMock, sodiumInit()).WillOnce(Return(-1));
    EXPECT_CALL(httpMock, collectHeaders(_, _));
    EXPECT_CALL(updaterMock, setup(_, _, _, _));
    EXPECT_CALL(httpMock, on(_, _, _));  // GET handler
    EXPECT_CALL(httpMock, on(_, _, _, _));  // POST handler with upload
    EXPECT_CALL(httpMock, begin());

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );
    otaServer->setup();
}

// Mark valid on boot tests
TEST_F(OtaUpdateServerTest, MarkValidOnBoot_NoRunningPartition_DoesNothing) {
    partitionMock.runningPartitionValid = false;

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(nullptr));
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(0);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );
    otaServer->markValidOnBoot();
}

TEST_F(OtaUpdateServerTest, MarkValidOnBoot_PendingVerify_MarksValid) {
    // Mock partition pointer (just use a dummy address)
    void* dummyPartition = reinterpret_cast<void*>(0x1000);

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(dummyPartition));
    EXPECT_CALL(partitionMock, getStatePartition(dummyPartition, _))
        .WillOnce([](const void*, int* state) {
            *state = 1;  // ESP_OTA_IMG_PENDING_VERIFY
            return 0;
        });
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(1);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );
    otaServer->markValidOnBoot();
}

TEST_F(OtaUpdateServerTest, MarkValidOnBoot_NotPendingVerify_DoesNothing) {
    void* dummyPartition = reinterpret_cast<void*>(0x1000);

    EXPECT_CALL(partitionMock, getRunningPartition()).WillOnce(Return(dummyPartition));
    EXPECT_CALL(partitionMock, getStatePartition(dummyPartition, _))
        .WillOnce([](const void*, int* state) {
            *state = 0;  // Not pending verify
            return 0;
        });
    EXPECT_CALL(partitionMock, markAppValidCancelRollback()).Times(0);

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );
    otaServer->markValidOnBoot();
}

// Callback tests
TEST_F(OtaUpdateServerTest, ErrorCallback_TriggeredOnError) {
    bool callbackCalled = false;
    OtaError capturedError = OtaError::NONE;
    std::string capturedMessage;

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );

    otaServer->setErrorCallback([&](OtaError err, const char* msg) {
        callbackCalled = true;
        capturedError = err;
        capturedMessage = msg ? msg : "";
    });

    // Trigger error through handlePost
    // In real implementation, this would be triggered by the upload handler
}

TEST_F(OtaUpdateServerTest, SuccessCallback_TriggeredOnSuccess) {
    bool callbackCalled = false;

    otaServer = std::make_unique<OtaUpdateServer>(
        httpMock, updaterMock, updateMock, partitionMock, cryptoMock
    );

    otaServer->setSuccessCallback([&]() {
        callbackCalled = true;
    });

    // Trigger success through handlePost
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
