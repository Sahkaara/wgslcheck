#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wgslcheck {

/**
 * @brief Represents a specific point in the source code.
 * 
 * Tracking exact line and column numbers is crucial for producing high-quality
 * error messages (e.g., "error at line 5, column 12"). This is the foundation
 * of a good developer experience.
 */
struct SourceLocation {
    std::size_t offset = 0; ///< Absolute byte offset from the start of the file.
    std::size_t line = 1;   ///< 1-based line number.
    std::size_t column = 1; ///< 1-based column number.
};

/**
 * @brief Represents a contiguous range of text in the source code.
 * 
 * A span is useful for highlighting errors in IDEs or for pointing to a 
 * specific token or expression during parsing and semantic analysis.
 */
struct SourceSpan {
    SourceLocation start;
    SourceLocation end;
};

/**
 * @brief Severity levels for diagnostics.
 */
enum class DiagnosticSeverity {
    warning, ///< Indicates a potential issue that doesn't halt execution.
    error,   ///< Indicates a fatal issue that prevents successful parsing/analysis.
};

/**
 * @brief A single diagnostic message emitted by the lexer, parser, or analyzer.
 */
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    std::string message;
    SourceLocation location;
};

/**
 * @brief Helper function to quickly check if a list of diagnostics contains errors.
 * 
 * @param diagnostics The list of diagnostics to inspect.
 * @return true if at least one diagnostic has `DiagnosticSeverity::error`.
 * @return false if the list only contains warnings or is empty.
 * 
 * @note Marked `noexcept` and `inline` since it's a trivial, highly frequent check 
 * that doesn't allocate memory or throw exceptions.
 */
inline bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::error) {
            return true;
        }
    }
    return false;
}

} // namespace wgslcheck