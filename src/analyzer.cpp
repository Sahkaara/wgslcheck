#include "wgslcheck/analyzer.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wgslcheck {
namespace {

/**
 * @brief Utility to find an attribute by name in a function's attribute list.
 * @return Pointer to the attribute if found, nullptr otherwise.
 */
const Attribute* find_attribute(const Function& function, const std::string& name) {
    for (const auto& attribute : function.attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

/**
 * @brief Parses a positive integer literal (e.g., "42", "0x1A", "10u").
 * @param text The raw literal string.
 * @param value Output parameter for the parsed value.
 * @return true if the string is a valid positive integer, false otherwise.
 */
bool parse_positive_integer(std::string text, std::uint32_t& value) {
    // Strip WGSL integer suffixes (e.g., 'u' for unsigned, 'i' for signed)
    while (!text.empty() && (text.back() == 'u' || text.back() == 'i')) {
        text.pop_back();
    }
    if (text.empty()) return false;
    
    int base = 10;
    const char* begin = text.data();
    
    // Handle hexadecimal literals
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        begin += 2;
    }
    
    const auto result = std::from_chars(begin, text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

/**
 * @brief Appends an error diagnostic to the analysis result.
 */
void add_error(AnalysisResult& result, std::string message, SourceLocation location) {
    result.diagnostics.push_back({DiagnosticSeverity::error, std::move(message), location});
}

// Forward declaration for recursive traversal.
void analyze_block(const Statement& block,
                   std::unordered_map<std::string, bool> variables,
                   AnalysisResult& result);

/**
 * @brief Analyzes a statement that creates a new lexical scope (like `if` or `else`).
 * Variables declared inside the branch should not leak into the outer scope.
 */
void analyze_scoped_statement(const Statement& statement,
                              const std::unordered_map<std::string, bool>& variables,
                              AnalysisResult& result) {
    if (statement.kind == Statement::Kind::block) {
        analyze_block(statement, variables, result);
    } else if (statement.kind == Statement::Kind::if_statement) {
        if (statement.then_branch) {
            analyze_scoped_statement(*statement.then_branch, variables, result);
        }
        if (statement.else_branch) {
            analyze_scoped_statement(*statement.else_branch, variables, result);
        }
    }
}

/**
 * @brief Recursively analyzes a block of statements.
 * @param variables A map of variable names to a boolean indicating mutability 
 * (true = immutable, false = mutable).
 */
void analyze_block(const Statement& block,
                   std::unordered_map<std::string, bool> variables,
                   AnalysisResult& result) {
    std::unordered_set<std::string> declared_here;
    
    for (const auto& statement : block.statements) {
        if (statement->kind == Statement::Kind::variable) {
            // Check for duplicate declarations in the same scope
            if (!declared_here.insert(statement->name).second) {
                add_error(result, "duplicate local variable '" + statement->name + "'",
                          statement->span.start);
            }
            // Track if the variable is immutable (let) or mutable (var)
            variables[statement->name] = !statement->mutable_variable;
            
        } else if (statement->kind == Statement::Kind::assignment &&
                   statement->expression &&
                   statement->expression->kind == Expression::Kind::identifier) {
            // Check for assignment to immutable values (let)
            const auto variable = variables.find(statement->expression->text);
            if (variable != variables.end() && variable->second) {
                add_error(result, "cannot assign to immutable value '" +
                          statement->expression->text + "'", statement->span.start);
            }
        } else if (statement->kind == Statement::Kind::block) {
            analyze_block(*statement, variables, result);
        } else if (statement->kind == Statement::Kind::if_statement) {
            analyze_scoped_statement(*statement, variables, result);
        }
    }
}

} // namespace

/**
 * @brief Performs semantic analysis on the entire module.
 */
AnalysisResult Analyzer::analyze(const Module& module) const {
    AnalysisResult result;
    std::unordered_map<std::string, SourceLocation> function_names;

    // NEW: Check for duplicate struct names
    std::unordered_set<std::string> struct_names;
    for (const auto& struct_node : module.structs) {
        if (!struct_names.insert(struct_node.name).second) {
            add_error(result, "duplicate struct '" + struct_node.name + "'",
                      struct_node.span.start);
        }
    }

    for (const auto& function : module.functions) {
        // 1. Check for duplicate function names
        const auto inserted = function_names.emplace(function.name, function.span.start);
        if (!inserted.second) {
            add_error(result, "duplicate function '" + function.name + "'",
                      function.span.start);
        }

        // 2. Check for duplicate function attributes
        std::unordered_set<std::string> attributes;
        for (const auto& item : function.attributes) {
            if (!attributes.insert(item.name).second) {
                add_error(result, "duplicate function attribute '@" + item.name + "'",
                          item.span.start);
            }
        }
        
        // 3. Check parameters
        std::unordered_set<std::string> parameters;
        for (const auto& parameter : function.parameters) {
            if (!parameters.insert(parameter.name).second) {
                add_error(result, "duplicate parameter '" + parameter.name + "'",
                          parameter.span.start);
            }
            std::unordered_set<std::string> parameter_attributes;
            for (const auto& item : parameter.attributes) {
                if (!parameter_attributes.insert(item.name).second) {
                    add_error(result, "duplicate parameter attribute '@" + item.name + "'",
                              item.span.start);
                }
            }
        }
        
        // 4. Analyze the function body
        if (function.body) {
            std::unordered_map<std::string, bool> variables;
            // Parameters are immutable by default
            for (const auto& function_parameter : function.parameters) {
                variables[function_parameter.name] = true;
            }
            analyze_block(*function.body, std::move(variables), result);
        }

        // 5. Validate shader-stage attributes
        const bool compute = find_attribute(function, "compute") != nullptr;
        const bool vertex = find_attribute(function, "vertex") != nullptr;
        const bool fragment = find_attribute(function, "fragment") != nullptr;
        const unsigned stage_count = static_cast<unsigned>(compute) +
                                     static_cast<unsigned>(vertex) +
                                     static_cast<unsigned>(fragment);
                                     
        if (stage_count > 1) {
            add_error(result, "a function cannot have more than one shader-stage attribute",
                      function.span.start);
        }
        if (stage_count == 0) continue; // Not an entry point, skip further checks

        // 6. Validate @workgroup_size for compute shaders
        const Attribute* workgroup = find_attribute(function, "workgroup_size");
        if (compute && workgroup == nullptr) {
            add_error(result, "compute entry point requires @workgroup_size",
                      function.span.start);
        } else if (!compute && workgroup != nullptr) {
            add_error(result, "@workgroup_size is only valid on compute entry points",
                      workgroup->span.start);
        } else if (compute && workgroup != nullptr) {
            if (workgroup->arguments.empty() || workgroup->arguments.size() > 3) {
                add_error(result, "@workgroup_size requires one to three positive integers",
                          workgroup->span.start);
                continue;
            }
            
            std::uint64_t product = 1;
            for (const auto& argument : workgroup->arguments) {
                std::uint32_t dimension = 0;
                if (!parse_positive_integer(argument, dimension)) {
                    add_error(result, "@workgroup_size values must be positive integer literals",
                              workgroup->span.start);
                    break;
                }
                if (dimension > 256) {
                    add_error(result, "a workgroup dimension cannot exceed 256",
                              workgroup->span.start);
                    break;
                }
                product *= dimension;
            }
            
            // WGSL spec: Max 256 invocations per workgroup
            if (product > 256) {
                add_error(result, "total workgroup size cannot exceed 256 invocations",
                          workgroup->span.start);
            }
            if (!function.return_type.empty()) {
                add_error(result, "compute entry points must not declare a return type",
                          function.span.start);
            }
        }
    }
    return result;
}

} // namespace wgslcheck