#include <cstdlib>
#include <iostream>
#include <string>

#include "wgslcheck/analyzer.hpp"
#include "wgslcheck/lexer.hpp"
#include "wgslcheck/parser.hpp"
#include "wgslcheck/simulator.hpp"

namespace {

/**
 * @brief Global counter for failed assertions.
 * 
 * Instead of using a heavy testing framework (like Google Test or Catch2),
 * this project uses a minimalistic, dependency-free approach. This keeps
 * build times fast and the binary small, which aligns with the project's 
 * zero-dependency philosophy.
 */
int failures = 0;

/**
 * @brief Basic assertion function.
 * @param condition The boolean condition that must be true.
 * @param message The error message to print if the condition is false.
 */
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

/**
 * @brief Helper function to run the entire frontend pipeline (Lexer + Parser).
 * 
 * @param source The raw WGSL code.
 * @param diagnostics Output parameter collecting errors from both stages.
 * @return The parsed Module AST.
 */
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

/**
 * @brief Tests the Lexer's edge cases.
 * Focuses on nested comments and EOF emission.
 */
void test_lexer() {
    // 1. Valid nested block comment
    wgslcheck::Lexer lexer("/* outer /* nested */ end */ @compute fn f() {}");
    const auto tokens = lexer.tokenize();
    expect(lexer.diagnostics().empty(), "nested block comments should lex");
    expect(tokens.front().kind == wgslcheck::TokenKind::at, "first token should be '@'");
    expect(tokens.back().kind == wgslcheck::TokenKind::end_of_file,
           "lexer should always emit end of file");

    // 2. Malformed comment (unterminated)
    wgslcheck::Lexer malformed("/* never closed");
    (void)malformed.tokenize();
    expect(wgslcheck::has_errors(malformed.diagnostics()),
           "unterminated block comment should be diagnosed");
}

/**
 * @brief A realistic WGSL shader used for end-to-end pipeline testing.
 * Includes hex literals (0x2u), vector swizzles (id.x), var declarations, 
 * if statements, and compound assignments (+=).
 */
const std::string valid_shader =
    "@compute @workgroup_size(0x2u, 1, 1)\n"
    "fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
    "  var value: u32 = id.x + 0x2u;\n"
    "  if (value > 0u) { value += 1u; }\n"
    "}\n";

/**
 * @brief Tests the full happy path: Parse -> Analyze -> Simulate.
 */
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
    
    // Verify that the simulator actually executed the correct number of invocations.
    // workgroup_size=(2,1,1) * dispatch=(1,1,1) = 2 invocations.
    if (!simulation.output.empty()) {
        expect(simulation.output[0].find("2 invocation(s)") != std::string::npos,
               "simulation should execute every local invocation");
    }
}

/**
 * @brief Tests semantic validation of @workgroup_size.
 * A dimension of 0 is syntactically valid but semantically illegal in WGSL.
 */
void test_invalid_workgroup_size() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse("@compute @workgroup_size(0) fn main() {}", diagnostics);
    
    expect(diagnostics.empty(), "zero workgroup size is syntactically valid");
    expect(!wgslcheck::Analyzer{}.analyze(module).ok(),
           "zero workgroup dimension should fail semantic analysis");
}

/**
 * @brief Tests edge cases where static analysis (Analyzer) and 
 * runtime execution (Simulator) diverge.
 * 
 * - Uninitialized variables are valid WGSL syntax (Analyzer passes) 
 *   but the CPU simulator doesn't support them (Simulator fails).
 * - Assigning to `let` variables is caught by both.
 */
void test_nested_generic_type_and_simulator_errors() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    
    // 1. Uninitialized variable (`var value: u32;`)
    auto module = parse(
        "fn helper(values: array<vec4<u32>>) {}\n"
        "@compute @workgroup_size(1) fn main() { var value: u32; }\n",
        diagnostics);
        
    expect(diagnostics.empty(), "nested generic types and uninitialized vars should parse");
    expect(wgslcheck::Analyzer{}.analyze(module).ok(),
           "uninitialized var is valid syntax for static checking");
           
    const auto simulation = wgslcheck::Simulator{}.run(module, {});
    expect(!simulation.ok(), "simulator should report unsupported uninitialized vars");

    // 2. Immutable assignment (`let value = 1u; value = 2u;`)
    auto immutable = parse(
        "@compute @workgroup_size(1) fn main() { let value = 1u; value = 2u; }\n",
        diagnostics);
        
    expect(diagnostics.empty(), "immutable assignment should reach semantic simulation");
    expect(!wgslcheck::Analyzer{}.analyze(immutable).ok(),
           "static analysis should reject assignment to let");
    expect(!wgslcheck::Simulator{}.run(immutable, {}).ok(),
           "simulator should reject assignment to let");
}

/**
 * @brief Tests function calls and compound bitwise shift assignments (`<<=`).
 */
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

/**
 * @brief Tests the simulator's strict enforcement of @builtin annotations.
 * 
 * WGSL allows entry point parameters without @builtin attributes (they become
 * pipeline-overridable constants or resources). However, the current CPU 
 * simulator only supports hardcoded built-in variables.
 */
void test_unsupported_builtin_is_reported() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "@compute @workgroup_size(1) fn main(id: u32) {}\n",
        diagnostics);
        
    expect(diagnostics.empty(), "function parameter syntax should parse");
    const auto simulation = wgslcheck::Simulator{}.run(module, {});
    expect(!simulation.ok(), "simulator should report missing builtin annotation");
}

void test_struct_parsing() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "struct VertexInput {\n"
        "  @location(0) position: vec3<f32>,\n"
        "  @location(1) color: vec4<f32>,\n"
        "}\n"
        "@compute @workgroup_size(1) fn main() {}\n",
        diagnostics);
        
    expect(diagnostics.empty(), "valid struct should parse without errors");
    expect(module.structs.size() == 1, "one struct should be parsed");
    
    if (!module.structs.empty()) {
        const auto& s = module.structs.front();
        expect(s.name == "VertexInput", "struct name should be correct");
        expect(s.members.size() == 2, "struct should have two members");
        expect(s.members[0].name == "position", "first member name should match");
        expect(s.members[0].type == "vec3<f32>", "first member type should match");
    }
}

} // namespace

int main() {
    // Run the entire test suite.
    test_lexer();
    test_parse_analyze_and_simulate();
    test_invalid_workgroup_size();
    test_nested_generic_type_and_simulator_errors();
    test_helper_calls_and_compound_shifts();
    test_unsupported_builtin_is_reported();
    test_struct_parsing();
    
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All wgslcheck tests passed\n";
    return EXIT_SUCCESS;
}
