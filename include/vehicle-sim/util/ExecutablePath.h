#pragma once

#include <string>
#include <vector>

namespace vehicle_sim::util {

/**
 * Cross-platform helper to locate the running executable and resolve resource
 * paths relative to it (instead of the caller's CWD/PWD).
 *
 * Shipped assets (e.g. resources/dbc/Model3CAN.dbc) live relative to the binary's
 * install location, so vehicle-sim must resolve them against the executable,
 * not the current working directory. This lets the binary run from any
 * directory (e.g. `cd /tmp && <repo>/build-native/vehicle-sim`).
 *
 * Executable directory resolution, in order of preference:
 *   - macOS:   _NSGetExecutablePath
 *   - Linux:   readlink("/proc/self/exe")
 *   - Windows: GetModuleFileNameW
 *   - fallback: empty string (caller falls back to PWD).
 */
class ExecutablePath {
public:
    /// Absolute directory containing the running executable (no trailing
    /// slash), or "" if it cannot be determined.
    [[nodiscard]] static std::string directory() noexcept;

    /// Resolve a resource path that is shipped relative to the executable's
    /// *install root* (e.g. "resources/dbc/Model3CAN.dbc").
    ///
    /// An absolute input is returned as-is (after an existence check) — it is
    /// never re-anchored under the executable directory or CWD. For a relative
    /// input, the search order (first existing file/dir wins) is:
    ///   1. Walk up from the executable's directory trying
    ///      <dir>/<relativeResourcePath> at each level, all the way to the
    ///      filesystem root (handles nested build dirs such as build-native/
    ///      and build-native/test/, at any checkout depth).
    ///   2. The current working directory: <cwd>/<relativeResourcePath>.
    ///
    /// @return absolute path to the first existing resource, or a best-effort
    ///         composed path (still install/PWD-relative) if none is found.
    [[nodiscard]] static std::string resolveResource(
        const std::string& relativeResourcePath) noexcept;

    /// Ordered list of every concrete path resolveResource() checks for a
    /// resource. An absolute input yields the input alone; a relative input
    /// yields <exeDir>/<rel>, then each ancestor directory up to the
    /// filesystem root, then <cwd>/<rel>. Consecutive duplicates (e.g.
    /// re-reaching the filesystem root) are collapsed. No existence checks
    /// are performed.
    ///
    /// Exposed so load-failure diagnostics can report the full candidate list
    /// ("paths tried") instead of a single opaque composed path.
    [[nodiscard]] static std::vector<std::string> resourceCandidates(
        const std::string& relativeResourcePath) noexcept;
};

} // namespace vehicle_sim::util
