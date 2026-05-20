#pragma once

#include <string>

namespace vehicle_sim::domain {

/**
 * Platform-agnostic DBC content loader.
 * On iOS, the bridge provides an implementation that reads from NSBundle.
 * On CLI/tests, a simple file reader is used.
 */
class IDBCContentProvider {
public:
    virtual ~IDBCContentProvider() = default;
    [[nodiscard]] virtual std::string loadContent(const std::string& fileName) const = 0;
};

/**
 * Default file-based DBC content provider for CLI and tests.
 */
class FileDBCContentProvider : public IDBCContentProvider {
public:
    explicit FileDBCContentProvider(const std::string& baseDir = "resources/dbc/");
    [[nodiscard]] std::string loadContent(const std::string& fileName) const override;

private:
    std::string baseDir_;
};

} // namespace vehicle_sim::domain