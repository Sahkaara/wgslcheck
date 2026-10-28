/*
 * version.hpp - single source of truth for the version string.
 *
 * The value comes from CMake (WGSLCHECK_VERSION). If the header is
 * included outside a CMake build (e.g. by a curious reader), we fall
 * back to a hard-coded string so the code still compiles.
 */
#pragma once

#ifndef WGSLCHECK_VERSION
#define WGSLCHECK_VERSION "0.1.0-dev"
#endif

namespace wgslcheck {

/* Return the library/CLI version, e.g. "0.1.0". */
inline constexpr const char* version() noexcept {
    return WGSLCHECK_VERSION;
}

} // namespace wgslcheck