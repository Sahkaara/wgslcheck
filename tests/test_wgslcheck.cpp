#include <cstdlib>
#include <iostream>
#include <string>

#include "wgslcheck/analyzer.hpp"
#include "wgslcheck/lexer.hpp"
#include "wgslcheck/parser.hpp"
#include "wgslcheck/simulator.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

wgslcheck::Module parse(const std::string& source,
                        std::vector<wgslcheck::Diagnostic>& diagnostics) {
    wgslcheck::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    diagnostics = lexer.diagnostics();
    wgslcheck::Parser parser(std::move(tokens));
    auto module = parser.parse();
    diagnostics.insert(diagnostics.end(), parser.diagnostics().begin(),
                       parser.diagnostics().end());
    return module;
}

void test_lexer() {
    wgslcheck::Lexer lexer("/* outer /* nested */ end */ @compute fn f() {}");
    const auto tokens = lexer.tokenize();
    expect(lexer.diagnostics().empty(), "nested block comments should lex");
    expect(tokens.front().kind == wgslcheck::TokenKind::at, "first token should be '@'");
    expect(tokens.back().kind == wgslcheck::TokenKind::end_of_file,
           "lexer should always emit end of file");

    wgslcheck::Lexer malformed("/* never closed");
    (void)malformed.tokenize();
    expect(wgslcheck::has_errors(malformed.diagnostics()),
           "unterminated block comment should be diagnosed");
}

const std::string valid_shader =
    "@compute @workgroup_size(0x2u, 1, 1)\n"
    "fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
    "  var value: u32 = id.x + 0x2u;\n"
    "  if (value > 0u) { value += 1u; }\n"
    "}\n";

void test_parse_analyze_and_simulate() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(valid_shader, diagnostics);
    expect(diagnostics.empty(), "valid compute shader should parse");
    expect(module.functions.size() == 1, "one function should be parsed");

    const auto analysis = wgslcheck::Analyzer{}.analyze(module);
    expect(analysis.ok(), "valid workgroup declaration should pass analysis");

    wgslcheck::SimulationOptions options;
    const auto simulation = wgslcheck::Simulator{}.run(module, options);
    expect(simulation.ok(), "supported compute shader should simulate");
    expect(simulation.output.size() == 1, "simulation should return one summary line");
    if (!simulation.output.empty()) {
        expect(simulation.output[0].find("2 invocation(s)") != std::string::npos,
               "simulation should execute every local invocation");
    }
}

void test_invalid_workgroup_size() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse("@compute @workgroup_size(0) fn main() {}", diagnostics);
    expect(diagnostics.empty(), "zero workgroup size is syntactically valid");
    expect(!wgslcheck::Analyzer{}.analyze(module).ok(),
           "zero workgroup dimension should fail semantic analysis");
}

void test_nested_generic_type_and_simulator_errors() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "fn helper(values: array<vec4<u32>>) {}\n"
        "@compute @workgroup_size(1) fn main() { var value: u32; }\n",
        diagnostics);
    expect(diagnostics.empty(), "nested generic types and uninitialized vars should parse");
    expect(wgslcheck::Analyzer{}.analyze(module).ok(),
           "uninitialized var is valid syntax for static checking");
    const auto simulation = wgslcheck::Simulator{}.run(module, {});
    expect(!simulation.ok(), "simulator should report unsupported uninitialized vars");

    auto immutable = parse(
        "@compute @workgroup_size(1) fn main() { let value = 1u; value = 2u; }\n",
        diagnostics);
    expect(diagnostics.empty(), "immutable assignment should reach semantic simulation");
    expect(!wgslcheck::Analyzer{}.analyze(immutable).ok(),
           "static analysis should reject assignment to let");
    expect(!wgslcheck::Simulator{}.run(immutable, {}).ok(),
           "simulator should reject assignment to let");
}

void test_helper_calls_and_compound_shifts() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "fn add(a: u32, b: u32) -> u32 { return a + b; }\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "  var value = add(1u, 2u);\n"
        "  if (value == 3u) { value <<= 1u; }\n"
        "}\n",
        diagnostics);
    expect(diagnostics.empty(), "helper call and compound shift should parse");
    const auto simulation = wgslcheck::Simulator{}.run(module, {});
    expect(simulation.ok(), "helper calls and compound shifts should simulate");
}

void test_unsupported_builtin_is_reported() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "@compute @workgroup_size(1) fn main(id: u32) {}\n",
        diagnostics);
    expect(diagnostics.empty(), "function parameter syntax should parse");
    const auto simulation = wgslcheck::Simulator{}.run(module, {});
    expect(!simulation.ok(), "simulator should report missing builtin annotation");
}

} // namespace

int main() {
    test_lexer();
    test_parse_analyze_and_simulate();
    test_invalid_workgroup_size();
    test_nested_generic_type_and_simulator_errors();
    test_helper_calls_and_compound_shifts();
    test_unsupported_builtin_is_reported();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All wgslcheck tests passed\n";
    return EXIT_SUCCESS;
}
