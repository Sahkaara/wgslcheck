#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wgslcheck {

struct SourceLocation {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct SourceSpan {
    SourceLocation start;
    SourceLocation end;
};

enum class DiagnosticSeverity {
    warning,
    error,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    std::string message;
    SourceLocation location;
};

inline bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::error) {
            return true;
        }
    }
    return false;
}

} // namespace wgslcheck
