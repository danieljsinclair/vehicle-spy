#pragma once

#include <string>
#include <vector>

// Test helper: owns a vector of argument strings and exposes a mutable
// char** argv() suitable for parseArgs(argc, argv). Backing strings stay
// alive for the lifetime of the Args object. Used by both CliOptionsTest and
// CliValidationTest (previously duplicated in CliOptions.test.cpp).
struct Args {
    std::vector<std::string> strings;
    std::vector<char*> ptrs;

    explicit Args(std::vector<std::string> args) : strings(std::move(args)) {
        ptrs.reserve(strings.size());
        for (auto& s : strings) {
            ptrs.push_back(s.data());
        }
    }

    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};
