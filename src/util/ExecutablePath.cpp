#include "vehicle-sim/util/ExecutablePath.h"

#include <filesystem>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace vehicle_sim::util {

namespace {

// Parent directory of `path` (no trailing slash), or "" at the filesystem root.
std::string parentDir(const std::string& path) {
    // Find last occurrence of either '/' or '\'
    std::string::size_type pos = std::string::npos;
    for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
        if (path[i] == '/' || path[i] == '\\') {
            pos = i;
            break;
        }
    }
    if (pos == std::string::npos) return "";
    if (pos == 0) return std::string(1, path[0]);  // Return "/" or "\"
    return path.substr(0, pos);
}

std::string joinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    if (dir.back() != '/' && dir.back() != '\\') {
        std::string out = dir;
        out.push_back('/');
        out += rel;
        return out;
    }
    return dir + rel;
}

} // namespace

std::string ExecutablePath::directory() noexcept {
#if defined(__APPLE__)
    std::array<char, PATH_MAX> buf{};
    auto size = static_cast<uint32_t>(buf.size());
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        // Buffer too small: size now holds the required length. Bail out.
        return "";
    }
    return parentDir(std::string(buf.data(), size));
#elif defined(__linux__)
    std::array<char, PATH_MAX> buf{};
    ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (len <= 0) return "";
    buf[static_cast<size_t>(len)] = '\0';
    return parentDir(std::string(buf.data()));
#elif defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buf{};
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(len),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(len),
                        out.data(), n, nullptr, nullptr);
    return parentDir(out);
#else
    return "";
#endif
}

std::string ExecutablePath::resolveResource(
    const std::string& relativeResourcePath) noexcept
{
    const std::string exeDir = directory();

    // 1. Walk up from the executable directory: <dir>/<relativeResourcePath>.
    //    Handles nested build dirs (build-native/, build-native/test/), so both
    //    the released binary and the test binary find the resource shipped
    //    under the project root regardless of CWD.
    if (!exeDir.empty()) {
        static constexpr int kMaxParentDirDepth = 8;
        std::string dir = exeDir;
        int depth = 0;
        while (depth < kMaxParentDirDepth && !dir.empty()) {
            const std::string candidate = joinPath(dir, relativeResourcePath);
            if (std::error_code ec; std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
            // If we got an error checking existence, continue to next directory
            dir = parentDir(dir);
            ++depth;
        }
    }

    // 2. Current working directory fallback (backward-compatible behaviour).
    {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            const std::string candidate = joinPath(cwd.string(), relativeResourcePath);
            std::error_code ec2;
            if (std::filesystem::exists(candidate, ec2)) {
                return candidate;
            }
        }
    }

    // Nothing found: return the install-relative best-effort path so the caller
    // produces a clear "not found" error rather than an empty path.
    if (!exeDir.empty()) {
        return joinPath(exeDir, relativeResourcePath);
    }
    return relativeResourcePath;
}

} // namespace vehicle_sim::util
