#pragma once

#include <string>
#include <vector>

#include "wgslcheck/ast.hpp"

namespace wgslcheck {

/**
 * @brief Aggregates the results of a static analysis pass.
 * 
 * Instead of returning a simple boolean or throwing exceptions, the Analyzer
 * returns a result object. This allows the caller to inspect all found issues 
 * (both errors and warnings) at once.
 */
struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;

    /**
     * @brief Convenience method to check if the analysis passed without fatal errors.
     * 
     * @return true if there are no `error` level diagnostics (warnings are fine).
     * @return false if at least one error was found.
     */
    bool ok() const noexcept { return !has_errors(diagnostics); }
};

/**
 * @brief The Analyzer performs semantic checks on the parsed AST.
 * 
 * While the Parser ensures the code is *syntactically* valid (e.g., matching 
 * parentheses and valid tokens), the Analyzer ensures it is *semantically* 
 * valid according to WGSL rules and best practices.
 * 
 * Examples of checks it performs:
 * - Invalid `@workgroup_size` declarations (e.g., zero or exceeding limits).
 * - Usage of undeclared variables.
 * - Missing `return` statements in non-void functions.
 * - Type mismatches in assignments.
 */
class Analyzer {
public:
    /**
     * @brief Runs the analysis pass over the entire AST module.
     * 
     * @param module The root AST node to analyze.
     * @return An `AnalysisResult` containing any errors or warnings found.
     * 
     * @note Marked `const` because analyzing the AST should never mutate it.
     */
    AnalysisResult analyze(const Module& module) const;
};

} // namespace wgslcheck