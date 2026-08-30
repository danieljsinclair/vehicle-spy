#pragma once

#include <string>

// Single source of truth for the `--examples` payload lives in
// `assets/examples.md` next to the top-level CMakeLists.txt. The body is
// embedded at build time into a generated translation unit
// (`<build>/generated/ExamplesContent.cpp`) via CMake `file(READ)` +
// `configure_file(@ONLY)`. This header is the public surface for the
// renderer in `CliOptions.cpp`.
//
// The string contains `# section:` / `# topic:` markers. The renderer parses
// them and produces the on-screen output (grouped under section headings,
// with a hierarchical focus filter).
namespace vehicle_sim::cli {

// Full EXAMPLES block, embedded from assets/examples.md.
extern const std::string kExamplesContent;

}  // namespace vehicle_sim::cli
