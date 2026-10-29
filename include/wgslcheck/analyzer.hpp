#pragma once

#include <string>
#include <vector>

#include "wgslcheck/ast.hpp"

namespace wgslcheck {

struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept { return !has_errors(diagnostics); }
};

class Analyzer {
public:
    AnalysisResult analyze(const Module& module) const;
};

} // namespace wgslcheck