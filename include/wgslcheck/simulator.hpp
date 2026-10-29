#pragma once

#include <array>
#include <string>
#include <vector>

#include "wgslcheck/ast.hpp"
#include "wgslcheck/diagnostic.hpp"

namespace wgslcheck {

struct SimulationOptions {
    std::array<unsigned, 3> workgroups{1, 1, 1};
    std::string entry_point;
    std::size_t max_invocations = 4096;
};

struct SimulationResult {
    std::vector<std::string> output;
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept { return !has_errors(diagnostics); }
};

class Simulator {
public:
    SimulationResult run(const Module& module, const SimulationOptions& options) const;
};

} // namespace wgslcheck