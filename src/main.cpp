/*
 * main.cpp - CLI entry point for wgslcheck.
 *
 * Commands (planned):
 *   wgslcheck check <file.wgsl>   Run static checks on a WGSL file
 *   wgslcheck sim   <file.wgsl>   Simulate workgroups on the CPU
 *
 * Exit codes:
 *   0 = success
 *   1 = usage error
 *   2 = analysis / simulation error
 *   3 = not implemented yet
 */
#include <cstdio>
#include <cstring>
#include <string_view>

#include "wgslcheck/version.hpp"

namespace {

/* Print the usage message to stderr. */
void usage() {
    std::fprintf(stderr,
        "wgslcheck %s - static analyzer and CPU-side debugger for WGSL\n"
        "\n"
        "Usage:\n"
        "  wgslcheck check <file.wgsl>   Run static checks\n"
        "  wgslcheck sim   <file.wgsl>   Simulate workgroups on the CPU\n"
        "  wgslcheck --version           Print the version\n"
        "  wgslcheck --help              Show this message\n",
        wgslcheck::version());
}

/* Handle --version and --help. Return true if the argument was consumed. */
bool handle_global_flag(std::string_view arg) {
    if (arg == "--version") {
        std::printf("wgslcheck %s\n", wgslcheck::version());
        return true;
    }
    if (arg == "--help" || arg == "-h") {
        usage();
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const std::string_view arg = argv[1];
    if (handle_global_flag(arg)) {
        return 0;
    }

    /* Subcommands are wired up here; bodies land in later commits. */
    if (arg == "check" || arg == "sim") {
        if (argc < 3) {
            std::fprintf(stderr,
                "error: '%s' requires a file argument\n", argv[1]);
            return 1;
        }
        std::fprintf(stderr,
            "wgslcheck: '%s' is not implemented yet (file: %s)\n",
            argv[1], argv[2]);
        return 3;
    }

    std::fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    usage();
    return 1;
}