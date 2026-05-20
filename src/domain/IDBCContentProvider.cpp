#include "vehicle-sim/domain/IDBCContentProvider.h"
#include <fstream>
#include <sstream>

namespace vehicle_sim::domain {

FileDBCContentProvider::FileDBCContentProvider(const std::string& baseDir)
    : baseDir_(baseDir)
{
}

std::string FileDBCContentProvider::loadContent(const std::string& fileName) const {
    std::string path = baseDir_ + fileName;
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace vehicle_sim::domain