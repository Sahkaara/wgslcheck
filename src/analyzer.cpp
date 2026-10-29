#include "wgslcheck/analyzer.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wgslcheck {
namespace {

const Attribute* find_attribute(const Function& function, const std::string& name) {
    for (const auto& attribute : function.attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

bool parse_positive_integer(std::string text, std::uint32_t& value) {
    while (!text.empty() && (text.back() == 'u' || text.back() == 'i')) {
        text.pop_back();
    }
    if (text.empty()) return false;
    int base = 10;
    const char* begin = text.data();
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        begin += 2;
    }
    const auto result = std::from_chars(begin, text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

void add_error(AnalysisResult& result, std::string message, SourceLocation location) {
    result.diagnostics.push_back({DiagnosticSeverity::error, std::move(message), location});
}

void analyze_block(const Statement& block,
                   std::unordered_map<std::string, bool> variables,
                   AnalysisResult& result);

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

void analyze_block(const Statement& block,
                   std::unordered_map<std::string, bool> variables,
                   AnalysisResult& result) {
    std::unordered_set<std::string> declared_here;
    for (const auto& statement : block.statements) {
        if (statement->kind == Statement::Kind::variable) {
            if (!declared_here.insert(statement->name).second) {
                add_error(result, "duplicate local variable '" + statement->name + "'",
                          statement->span.start);
            }
            variables[statement->name] = !statement->mutable_variable;
        } else if (statement->kind == Statement::Kind::assignment &&
                   statement->expression &&
                   statement->expression->kind == Expression::Kind::identifier) {
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

AnalysisResult Analyzer::analyze(const Module& module) const {
    AnalysisResult result;
    std::unordered_map<std::string, SourceLocation> function_names;

    for (const auto& function : module.functions) {
        const auto inserted = function_names.emplace(function.name, function.span.start);
        if (!inserted.second) {
            add_error(result, "duplicate function '" + function.name + "'",
                      function.span.start);
        }

        std::unordered_set<std::string> attributes;
        for (const auto& item : function.attributes) {
            if (!attributes.insert(item.name).second) {
                add_error(result, "duplicate function attribute '@" + item.name + "'",
                          item.span.start);
            }
        }
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
        if (function.body) {
            std::unordered_map<std::string, bool> variables;
            for (const auto& function_parameter : function.parameters) {
                variables[function_parameter.name] = true;
            }
            analyze_block(*function.body, std::move(variables), result);
        }

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
        if (stage_count == 0) continue;

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