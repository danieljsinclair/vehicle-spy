#pragma once

// ArduinoHttpServer.h - Arduino WebServer implementation for IHttpServer.
// Bridges the ESP32 WebServer library to the vanilla IHttpServer interface used
// by OtaUpdateServer.
//
// Gap 5 (upload supply): WebServer surfaces the in-flight multipart upload via
// upload() (HTTPUpload&). This adapter's upload() snapshots it into a vanilla
// IHttpUpload (status enum + buf + currentSize + totalSize + name/filename) so
// the vanilla's upload-handler closure can drive the START/WRITE/END/ABORTED
// state machine without carrying Arduino types.
//
// Method mapping: the vanilla uses its own int codes (1=GET, 2=POST) so it
// carries no http_parser header; the adapter maps them back to HTTPMethod.
//
// Production implementation used in the .ino. Host tests use MockHttpServer.
// Only available when building for Arduino (ARDUINO defined).

#ifdef ARDUINO

#include <memory>
#include <string>
#include "OtaUpdateServer.h"
#include <WebServer.h>

namespace esp32_firmware {

class ArduinoHttpServer : public IHttpServer {
public:
    explicit ArduinoHttpServer(uint16_t port) : server_(port) {}
    // Allow the .ino to reach the concrete WebServer (e.g. to share with the
    // HTTPUpdateServer adapter that needs a WebServer&).
    WebServer& raw() { return server_; }

    void collectHeaders(const char** headers, size_t count) override {
        server_.collectHeaders(headers, count);
    }

    void on(const char* uri, int method, std::function<void()> handler) override {
        server_.on(uri, toHttpMethod(method), std::move(handler));
    }

    void on(const char* uri, int method,
            std::function<void()> postHandler,
            std::function<void()> uploadHandler) override {
        server_.on(uri, toHttpMethod(method),
                   std::move(postHandler), std::move(uploadHandler));
    }

    void begin() override { server_.begin(); }
    void handleClient() override { server_.handleClient(); }

    void send(int code, const char* contentType, const char* content) override {
        server_.send(code, contentType, content);
    }

    std::string header(const char* name) override {
        const String v = server_.header(name);
        return std::string(v.c_str());
    }

    void clientSetNoDelay(bool nodelay) override {
        server_.client().setNoDelay(nodelay);
    }

    void clientFlush() override {
        server_.client().flush();
    }

    void clientStop() override {
        server_.client().stop();
    }

    // Gap 5: snapshot the in-flight HTTPUpload into a vanilla IHttpUpload. The
    // WebServer owns the upload; the returned snapshot is valid for the duration
    // of the upload-handler closure invocation (buf is copied so the snapshot is
    // self-owned, not dangling after WebServer reuses its upload slot).
    std::unique_ptr<IHttpUpload> upload() override {
        HTTPUpload& src = server_.upload();
        auto dst = std::make_unique<IHttpUpload>();
        dst->status = static_cast<IHttpUpload::Status>(src.status);
        // String storage in HTTPUpload is owned by the WebServer; copy into the
        // snapshot so it survives the closure return.
        nameBuf_ = std::string(src.name.c_str());
        fileBuf_ = std::string(src.filename.c_str());
        dst->name = nameBuf_.c_str();
        dst->filename = fileBuf_.c_str();
        dst->buf = src.buf;
        dst->currentSize = src.currentSize;
        dst->totalSize = src.totalSize;
        return dst;
    }

private:
    WebServer server_;
    // Backing storage for the snapshot's c_str pointers (kept on the adapter so
    // the returned IHttpUpload's name/filename remain valid through the closure).
    std::string nameBuf_;
    std::string fileBuf_;

    static HTTPMethod toHttpMethod(int method) {
        // Vanilla convention: 1 = GET, 2 = POST (see OtaUpdateServer::setup).
        switch (method) {
            case 1:  return HTTP_GET;
            case 2:  return HTTP_POST;
            default: return HTTP_ANY;
        }
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
