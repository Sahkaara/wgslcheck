#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "wgslcheck/analyzer.hpp"
#include "wgslcheck/lexer.hpp"
#include "wgslcheck/parser.hpp"
#include "wgslcheck/simulator.hpp"
#include "wgslcheck/version.hpp"

namespace {

void usage() {
    std::cerr << "wgslcheck " << wgslcheck::version()
              << " - WGSL static checker and CPU simulator\n\n"
              << "Usage:\n"
              << "  wgslcheck check <file.wgsl>\n"
              << "  wgslcheck sim <file.wgsl> [--entry NAME] [--dispatch X Y Z]\n"
              << "  wgslcheck --version\n"
              << "  wgslcheck --help\n";
}

void print_diagnostics(const std::string& path,
                       const std::vector<wgslcheck::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        std::cerr << path << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": "
                  << (diagnostic.severity == wgslcheck::DiagnosticSeverity::error
                          ? "error: " : "warning: ")
                  << diagnostic.message << '\n';
    }
}

bool parse_positive(std::string_view text, unsigned& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value > 0;
}

int run(const std::string& command, const std::string& path,
        const wgslcheck::SimulationOptions& simulation_options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "error: cannot open '" << path << "'\n";
        return 2;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (input.bad()) {
        std::cerr << "error: failed while reading '" << path << "'\n";
        return 2;
    }

    wgslcheck::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    const auto lexer_diagnostics = lexer.diagnostics();
    wgslcheck::Parser parser(std::move(tokens));
    auto module = parser.parse();
    std::vector<wgslcheck::Diagnostic> diagnostics = lexer_diagnostics;
    diagnostics.insert(diagnostics.end(), parser.diagnostics().begin(),
                       parser.diagnostics().end());
    if (wgslcheck::has_errors(diagnostics)) {
        print_diagnostics(path, diagnostics);
        return 2;
    }

    wgslcheck::Analyzer analyzer;
    auto analysis = analyzer.analyze(module);
    if (!analysis.ok()) {
        print_diagnostics(path, analysis.diagnostics);
        return 2;
    }
    if (command == "check") {
        std::cout << path << ": no issues found (" << module.functions.size()
                  << " function(s) checked)\n";
        return 0;
    }

    wgslcheck::Simulator simulator;
    auto simulation = simulator.run(module, simulation_options);
    if (!simulation.ok()) {
        print_diagnostics(path, simulation.diagnostics);
        return 2;
    }
    for (const auto& line : simulation.output) std::cout << line << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const std::string_view arg = argv[1];
    if (arg == "--version") {
        std::cout << "wgslcheck " << wgslcheck::version() << '\n';
        return 0;
    }
    if (arg == "--help" || arg == "-h") {
        usage();
        return 0;
    }
    if (arg != "check" && arg != "sim") {
        std::cerr << "error: unknown command '" << argv[1] << "'\n";
        usage();
        return 1;
    }
    if (argc < 3) {
        std::cerr << "error: '" << argv[1] << "' requires a file argument\n";
        usage();
        return 1;
    }

    wgslcheck::SimulationOptions options;
    for (int index = 3; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (arg != "sim") {
            std::cerr << "error: unexpected argument '" << option << "' for check\n";
            return 1;
        }
        if (option == "--entry") {
            if (++index >= argc || argv[index][0] == '\0') {
                std::cerr << "error: --entry requires a function name\n";
                return 1;
            }
            options.entry_point = argv[index];
        } else if (option == "--dispatch") {
            if (index + 3 >= argc) {
                std::cerr << "error: --dispatch requires three positive integers\n";
                return 1;
            }
            for (std::size_t dimension = 0; dimension < 3; ++dimension) {
                if (!parse_positive(argv[++index], options.workgroups[dimension])) {
                    std::cerr << "error: --dispatch values must be positive integers\n";
                    return 1;
                }
            }
        } else {
            std::cerr << "error: unknown sim option '" << option << "'\n";
            return 1;
        }
    }
    return run(std::string(arg), argv[2], options);
}