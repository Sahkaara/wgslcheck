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

void test_numeric_suffix_validation() {
    const char* invalid[] = {"0x1uu", "0x1ui", "0x1ii", "0xffiu",
                             "1.5u", "1.5i", "1e2u", "1e2i"};
    for (const auto* literal : invalid) {
        wgslcheck::Lexer lexer(literal);
        const auto tokens = lexer.tokenize();
        expect(wgslcheck::has_errors(lexer.diagnostics()), literal);
        expect(tokens.back().kind == wgslcheck::TokenKind::end_of_file,
               "malformed numeric suffix must still reach EOF");
        if (!lexer.diagnostics().empty()) {
            expect(lexer.diagnostics().front().location.line == 1 &&
                   lexer.diagnostics().front().location.column == 1,
                   "numeric suffix diagnostic should point to the literal");
        }
    }
    const char* valid[] = {"0xff", "0xffu", "0x1i", "12u", "12i",
                           "1.5f", "1.5h", "1e2f", "1e2"};
    for (const auto* literal : valid) {
        wgslcheck::Lexer lexer(literal);
        const auto tokens = lexer.tokenize();
        expect(lexer.diagnostics().empty(), literal);
        expect(tokens.size() == 2 && tokens.front().lexeme == literal,
               "valid numeric literal must remain a single intact token");
    }
}

void test_else_if_execution() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "fn choose(value: u32) -> u32 {\n"
        "  if (value == 0u) { return 10u; }\n"
        "  else if (value == 1u) { return 20u; }\n"
        "  else if (value == 2u) { return 30u; }\n"
        "  else { return 40u; }\n"
        "}\n"
        "@compute @workgroup_size(4)\n"
        "fn main(@builtin(local_invocation_index) index: u32) {\n"
        "  if (choose(index) != (index + 1u) * 10u) { let failure = 1 / 0; }\n"
        "  var value = 0u;\n"
        "  if (false) { value = 1u; } else if (false) { value = 2u; }\n"
        "  if (value != 0u) { let failure = 1 / 0; }\n"
        "  if (true) { value = 3u; } else if (1 / 0 == 0) { value = 4u; }\n"
        "  if (value != 3u) { let failure = 1 / 0; }\n"
        "}\n", diagnostics);
    expect(diagnostics.empty(), "else-if regression should parse");
    expect(wgslcheck::Analyzer{}.analyze(module).ok(),
           "else-if regression should pass analysis");
    expect(wgslcheck::Simulator{}.run(module, {}).ok(),
           "else-if chains must select the correct branch and propagate returns");
}

void test_function_mutability_isolation() {
    std::vector<wgslcheck::Diagnostic> diagnostics;
    auto module = parse(
        "fn helper() -> u32 { var value = 1u; value += 1u; return value; }\n"
        "@compute @workgroup_size(2) fn main() {\n"
        "  let value = 10u;\n"
        "  if (helper() != 2u) { let failure = 1 / 0; }\n"
        "}\n", diagnostics);
    expect(diagnostics.empty(), "independent function locals should parse");
    expect(wgslcheck::Analyzer{}.analyze(module).ok(),
           "independent function locals should pass analysis");
    expect(wgslcheck::Simulator{}.run(module, {}).ok(),
           "caller let must not make a helper local immutable");

    auto immutable = parse(
        "fn helper() { var value = 1u; }\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "  let value = 10u; helper(); value = 20u;\n"
        "}\n", diagnostics);
    expect(diagnostics.empty(), "caller mutability regression should parse");
    const auto result = wgslcheck::Simulator{}.run(immutable, {});
    expect(!result.ok(), "helper cleanup must preserve caller immutability");
    if (!result.diagnostics.empty()) {
        expect(result.diagnostics.front().message.find("immutable") != std::string::npos,
               "caller assignment must report immutability");
    }
}

void test_numeric_literal_values() {
    const char* cases[] = {
        "0xf == 15", "0xff == 255", "0xffu == 255u",
        "0x1fi == 31i", "0xFF == 255", "0Xff == 255",
        "1.5f == 1.5", "2.0h == 2.0", "1e2f == 100.0"
    };
    for (const auto* condition : cases) {
        std::vector<wgslcheck::Diagnostic> diagnostics;
        auto module = parse(
            std::string("@compute @workgroup_size(1) fn main() { if (!(") +
            condition + ")) { let failure = 1 / 0; } }", diagnostics);
        expect(diagnostics.empty(), "numeric literal regression should parse");
        expect(wgslcheck::Simulator{}.run(module, {}).ok(), condition);
    }
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
    test_numeric_literal_values();
    test_function_mutability_isolation();
    test_else_if_execution();
    test_numeric_suffix_validation();
    
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All wgslcheck tests passed\n";
    return EXIT_SUCCESS;
}
