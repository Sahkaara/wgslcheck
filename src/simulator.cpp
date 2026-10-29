#include "wgslcheck/simulator.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace wgslcheck {
namespace {

struct Value {
    enum class Kind { number, boolean, vector };
    Kind kind = Kind::number;
    double number = 0.0;
    bool boolean = false;
    std::vector<double> components;

    static Value scalar(double value) {
        Value result;
        result.number = value;
        return result;
    }

    static Value boolean_value(bool value) {
        Value result;
        result.kind = Kind::boolean;
        result.boolean = value;
        return result;
    }

    static Value vector_value(std::vector<double> values) {
        Value result;
        result.kind = Kind::vector;
        result.components = std::move(values);
        return result;
    }
};

struct ReturnSignal {
    Value value;
};

bool parse_integer(std::string text, unsigned& result) {
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
    const auto parsed = std::from_chars(begin, text.data() + text.size(), result, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && result > 0;
}

bool truthy(const Value& value) {
    if (value.kind == Value::Kind::boolean) return value.boolean;
    if (value.kind == Value::Kind::number) return value.number != 0.0;
    return !value.components.empty();
}

class Interpreter {
public:
    Interpreter(const Module& module, SimulationResult& result)
        : result_(result) {
        for (const auto& function : module.functions) {
            functions_.emplace(function.name, &function);
        }
    }

    void invoke(const Function& function, const std::array<unsigned, 3>& group,
                const std::array<unsigned, 3>& local,
                const std::array<unsigned, 3>& dispatch,
                const std::array<unsigned, 3>& workgroup_size) {
        std::unordered_map<std::string, Value> environment;
        const std::array<unsigned, 3> global{
            group[0] * workgroup_size[0] + local[0],
            group[1] * workgroup_size[1] + local[1],
            group[2] * workgroup_size[2] + local[2]};
        const unsigned linear = local[0] + workgroup_size[0] *
            (local[1] + workgroup_size[1] * local[2]);

        try {
            if (!function.body) {
                fail("compute entry point has no function body", function.span.start);
            }
            for (const auto& parameter : function.parameters) {
                const Attribute* builtin = nullptr;
                for (const auto& attribute : parameter.attributes) {
                    if (attribute.name == "builtin") {
                        builtin = &attribute;
                        break;
                    }
                }
                if (builtin == nullptr || builtin->arguments.size() != 1) {
                    fail("simulator requires each entry-point parameter to have a supported @builtin",
                         parameter.span.start);
                }
                const std::string& name = builtin->arguments.front();
                mark_immutable(parameter.name);
                if (name == "global_invocation_id") {
                    environment.emplace(parameter.name, vector(global));
                } else if (name == "local_invocation_id") {
                    environment.emplace(parameter.name, vector(local));
                } else if (name == "workgroup_id") {
                    environment.emplace(parameter.name, vector(group));
                } else if (name == "num_workgroups") {
                    environment.emplace(parameter.name, vector(dispatch));
                } else if (name == "local_invocation_index") {
                    environment.emplace(parameter.name, Value::scalar(linear));
                } else {
                    fail("unsupported compute builtin '@builtin(" + name + ")'",
                         builtin->span.start);
                }
            }

            execute_block(*function.body, environment, 0);
        } catch (const ReturnSignal&) {
        } catch (const RuntimeFailure&) {
            return;
        }
    }

private:
    struct RuntimeFailure {};

    static Value vector(const std::array<unsigned, 3>& values) {
        return Value::vector_value({static_cast<double>(values[0]),
                                    static_cast<double>(values[1]),
                                    static_cast<double>(values[2])});
    }

    [[noreturn]] void fail(const std::string& message, SourceLocation location) {
        result_.diagnostics.push_back({DiagnosticSeverity::error, message, location});
        throw RuntimeFailure{};
    }

    double number(const Value& value, SourceLocation location) {
        if (value.kind != Value::Kind::number) fail("expected a scalar number", location);
        return value.number;
    }

    std::int64_t integer(double value, SourceLocation location) {
        if (!std::isfinite(value) || std::trunc(value) != value ||
            value < -9223372036854775808.0 ||
            value >= 9223372036854775808.0) {
            fail("bitwise operations require an in-range integer value", location);
        }
        return static_cast<std::int64_t>(value);
    }

    void mark_immutable(const std::string& name) {
        ++immutable_[name];
    }

    void release_immutable(const std::string& name) {
        const auto found = immutable_.find(name);
        if (found != immutable_.end() && --found->second == 0) {
            immutable_.erase(found);
        }
    }

    Value evaluate(const Expression& expression,
                   std::unordered_map<std::string, Value>& environment,
                   unsigned call_depth) {
        switch (expression.kind) {
        case Expression::Kind::number: {
            std::string literal = expression.text;
            while (!literal.empty() && (literal.back() == 'u' || literal.back() == 'i' ||
                                        literal.back() == 'f' || literal.back() == 'h')) {
                literal.pop_back();
            }
            try {
                std::size_t consumed = 0;
                const double value = std::stod(literal, &consumed);
                if (consumed != literal.size() || !std::isfinite(value)) {
                    fail("invalid numeric literal for simulator", expression.span.start);
                }
                return Value::scalar(value);
            } catch (const RuntimeFailure&) {
                throw;
            } catch (const std::invalid_argument&) {
                fail("invalid numeric literal for simulator", expression.span.start);
            } catch (const std::out_of_range&) {
                fail("invalid numeric literal for simulator", expression.span.start);
            }
        }
        case Expression::Kind::boolean:
            return Value::boolean_value(expression.text == "true");
        case Expression::Kind::identifier: {
            const auto found = environment.find(expression.text);
            if (found == environment.end()) {
                fail("unknown variable '" + expression.text + "'", expression.span.start);
            }
            return found->second;
        }
        case Expression::Kind::member: {
            Value base = evaluate(*expression.children[0], environment, call_depth);
            if (base.kind != Value::Kind::vector) {
                fail("member access is only supported on vectors", expression.span.start);
            }
            const std::string_view member = expression.text;
            const std::size_t index = member == "x" || member == "r" ? 0 :
                                      member == "y" || member == "g" ? 1 :
                                      member == "z" || member == "b" ? 2 :
                                      member == "w" || member == "a" ? 3 :
                                      std::numeric_limits<std::size_t>::max();
            if (index >= base.components.size()) {
                fail("invalid vector component '" + expression.text + "'", expression.span.start);
            }
            return Value::scalar(base.components[index]);
        }
        case Expression::Kind::unary: {
            const Value operand = evaluate(*expression.children[0], environment, call_depth);
            if (expression.text == "!") return Value::boolean_value(!truthy(operand));
            const double n = number(operand, expression.span.start);
            if (expression.text == "-") return Value::scalar(-n);
            if (expression.text == "+") return Value::scalar(n);
            if (expression.text == "~") {
                return Value::scalar(static_cast<double>(~integer(n, expression.span.start)));
            }
            fail("unsupported unary operator", expression.span.start);
        }
        case Expression::Kind::binary: {
            const Value left = evaluate(*expression.children[0], environment, call_depth);
            if (expression.text == "&&" && !truthy(left)) return Value::boolean_value(false);
            if (expression.text == "||" && truthy(left)) return Value::boolean_value(true);
            const Value right = evaluate(*expression.children[1], environment, call_depth);
            if (expression.text == "&&") return Value::boolean_value(truthy(right));
            if (expression.text == "||") return Value::boolean_value(truthy(right));
            if (expression.text == "==" || expression.text == "!=") {
                bool equal = false;
                if (left.kind == Value::Kind::boolean && right.kind == Value::Kind::boolean) {
                    equal = left.boolean == right.boolean;
                } else if (left.kind == Value::Kind::number && right.kind == Value::Kind::number) {
                    equal = left.number == right.number;
                } else {
                    fail("equality operands must have matching scalar types",
                         expression.span.start);
                }
                return Value::boolean_value(expression.text == "==" ? equal : !equal);
            }
            const double a = number(left, expression.span.start);
            const double b = number(right, expression.span.start);
            if (expression.text == "+") return Value::scalar(a + b);
            if (expression.text == "-") return Value::scalar(a - b);
            if (expression.text == "*") return Value::scalar(a * b);
            if (expression.text == "/") {
                if (b == 0.0) fail("division by zero", expression.span.start);
                return Value::scalar(a / b);
            }
            if (expression.text == "%") {
                if (b == 0.0) fail("remainder by zero", expression.span.start);
                return Value::scalar(std::fmod(a, b));
            }
            if (expression.text == "<") return Value::boolean_value(a < b);
            if (expression.text == "<=") return Value::boolean_value(a <= b);
            if (expression.text == ">") return Value::boolean_value(a > b);
            if (expression.text == ">=") return Value::boolean_value(a >= b);
            if (expression.text == "&") return Value::scalar(static_cast<double>(
                integer(a, expression.span.start) & integer(b, expression.span.start)));
            if (expression.text == "|") return Value::scalar(static_cast<double>(
                integer(a, expression.span.start) | integer(b, expression.span.start)));
            if (expression.text == "^") return Value::scalar(static_cast<double>(
                integer(a, expression.span.start) ^ integer(b, expression.span.start)));
            if (expression.text == "<<" || expression.text == ">>") {
                const auto shift = integer(b, expression.span.start);
                if (shift < 0 || shift >= 64) {
                    fail("shift amount must be between 0 and 63", expression.span.start);
                }
                const auto operand = static_cast<std::uint64_t>(integer(a, expression.span.start));
                const auto shifted = expression.text == "<<"
                    ? operand << shift : operand >> shift;
                return Value::scalar(static_cast<double>(static_cast<std::int64_t>(shifted)));
            }
            fail("unsupported binary operator '" + expression.text + "'", expression.span.start);
        }
        case Expression::Kind::call:
            return call(expression, environment, call_depth);
        }
        fail("unsupported expression", expression.span.start);
    }

    Value call(const Expression& expression,
               std::unordered_map<std::string, Value>& environment,
               unsigned call_depth) {
        const Expression& callee = *expression.children.front();
        if (callee.kind != Expression::Kind::identifier) {
            fail("only named function calls are supported", callee.span.start);
        }
        const std::string& name = callee.text;
        std::vector<Value> arguments;
        for (std::size_t i = 1; i < expression.children.size(); ++i) {
            arguments.push_back(evaluate(*expression.children[i], environment, call_depth));
        }
        if (name == "u32" || name == "i32" || name == "f32" ||
            name == "f16" || name == "bool") {
            if (arguments.size() != 1) fail("scalar conversion expects one argument", expression.span.start);
            if (name == "bool") return Value::boolean_value(truthy(arguments[0]));
            return Value::scalar(number(arguments[0], expression.span.start));
        }
        if (name.rfind("vec", 0) == 0 && name.size() == 4 &&
            name[3] >= '2' && name[3] <= '4') {
            const std::size_t width = static_cast<std::size_t>(name[3] - '0');
            std::vector<double> components;
            for (const auto& argument : arguments) {
                if (argument.kind == Value::Kind::vector) {
                    components.insert(components.end(), argument.components.begin(),
                                      argument.components.end());
                } else {
                    components.push_back(number(argument, expression.span.start));
                }
            }
            if (components.size() == 1) components.resize(width, components.front());
            if (components.size() != width) {
                fail("vector constructor has the wrong number of components",
                     expression.span.start);
            }
            return Value::vector_value(std::move(components));
        }
        if (name == "min" || name == "max") {
            if (arguments.size() != 2) fail(name + " expects two arguments", expression.span.start);
            const double a = number(arguments[0], expression.span.start);
            const double b = number(arguments[1], expression.span.start);
            return Value::scalar(name == "min" ? std::min(a, b) : std::max(a, b));
        }
        if (name == "abs" || name == "sqrt" || name == "floor" || name == "ceil") {
            if (arguments.size() != 1) fail(name + " expects one argument", expression.span.start);
            const double n = number(arguments[0], expression.span.start);
            if (name == "sqrt" && n < 0.0) fail("sqrt argument cannot be negative", expression.span.start);
            if (name == "abs") return Value::scalar(std::abs(n));
            if (name == "sqrt") return Value::scalar(std::sqrt(n));
            if (name == "floor") return Value::scalar(std::floor(n));
            return Value::scalar(std::ceil(n));
        }

        const auto function = functions_.find(name);
        if (function == functions_.end()) {
            fail("unsupported function call '" + name + "'", expression.span.start);
        }
        if (call_depth >= 32) fail("maximum function call depth exceeded", expression.span.start);
        const Function& target = *function->second;
        if (arguments.size() != target.parameters.size()) {
            fail("wrong number of arguments for function '" + name + "'", expression.span.start);
        }
        std::unordered_map<std::string, Value> locals;
        std::vector<std::string> immutable_parameters;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            locals.emplace(target.parameters[i].name, arguments[i]);
            mark_immutable(target.parameters[i].name);
            immutable_parameters.push_back(target.parameters[i].name);
        }
        try {
            execute_block(*target.body, locals, call_depth + 1);
        } catch (const ReturnSignal& returned) {
            for (const auto& parameter : immutable_parameters) release_immutable(parameter);
            return returned.value;
        } catch (const RuntimeFailure&) {
            for (const auto& parameter : immutable_parameters) release_immutable(parameter);
            throw;
        }
        for (const auto& parameter : immutable_parameters) release_immutable(parameter);
        return Value::scalar(0.0);
    }

    void execute_block(const Statement& block,
                       std::unordered_map<std::string, Value>& environment,
                       unsigned call_depth) {
        std::vector<std::string> declared;
        try {
            for (const auto& statement : block.statements) {
                if (statement->kind == Statement::Kind::variable) {
                    if (environment.count(statement->name) != 0) {
                        fail("variable '" + statement->name + "' is already declared",
                             statement->span.start);
                    }
                    if (statement->expression == nullptr) {
                        fail("simulator does not support uninitialized variables",
                             statement->span.start);
                    }
                    environment.emplace(statement->name,
                        evaluate(*statement->expression, environment, call_depth));
                    declared.push_back(statement->name);
                    if (!statement->mutable_variable) mark_immutable(statement->name);
                    continue;
                }
                if (statement->kind == Statement::Kind::assignment) {
                    if (!statement->expression ||
                        statement->expression->kind != Expression::Kind::identifier ||
                        statement->expression->children.empty()) {
                        fail("simulator assignment target must be a local variable",
                             statement->span.start);
                    }
                    const std::string& name = statement->expression->text;
                    auto target = environment.find(name);
                    if (target == environment.end()) fail("unknown variable '" + name + "'", statement->span.start);
                    if (immutable_.count(name) != 0) {
                        fail("cannot assign to immutable value '" + name + "'",
                             statement->span.start);
                    }
                    const Value right = evaluate(*statement->expression->children.front(),
                                                 environment, call_depth);
                    if (statement->operation == "=") {
                        target->second = right;
                    } else {
                        const double left_number = number(target->second, statement->span.start);
                        const double right_number = number(right, statement->span.start);
                        const std::string operation = statement->operation.substr(
                            0, statement->operation.size() - 1);
                        if (operation == "+") target->second = Value::scalar(left_number + right_number);
                        else if (operation == "-") target->second = Value::scalar(left_number - right_number);
                        else if (operation == "*") target->second = Value::scalar(left_number * right_number);
                        else if (operation == "/") {
                            if (right_number == 0.0) fail("division by zero", statement->span.start);
                            target->second = Value::scalar(left_number / right_number);
                        } else if (operation == "%") {
                            if (right_number == 0.0) fail("remainder by zero", statement->span.start);
                            target->second = Value::scalar(std::fmod(left_number, right_number));
                        } else if (operation == "&") {
                            target->second = Value::scalar(static_cast<double>(
                                integer(left_number, statement->span.start) &
                                integer(right_number, statement->span.start)));
                        } else if (operation == "|") {
                            target->second = Value::scalar(static_cast<double>(
                                integer(left_number, statement->span.start) |
                                integer(right_number, statement->span.start)));
                        } else if (operation == "^") {
                            target->second = Value::scalar(static_cast<double>(
                                integer(left_number, statement->span.start) ^
                                integer(right_number, statement->span.start)));
                        } else if (operation == "<<" || operation == ">>") {
                            const auto shift = integer(right_number, statement->span.start);
                            if (shift < 0 || shift >= 64) {
                                fail("shift amount must be between 0 and 63",
                                     statement->span.start);
                            }
                            const auto operand = static_cast<std::uint64_t>(
                                integer(left_number, statement->span.start));
                            target->second = Value::scalar(static_cast<double>(
                                static_cast<std::int64_t>(operation == "<<"
                                    ? operand << shift : operand >> shift)));
                        } else {
                            fail("unsupported assignment operator", statement->span.start);
                        }
                    }
                    continue;
                }
                if (statement->kind == Statement::Kind::return_statement) {
                    throw ReturnSignal{statement->expression
                        ? evaluate(*statement->expression, environment, call_depth)
                        : Value::scalar(0.0)};
                }
                if (statement->kind == Statement::Kind::if_statement) {
                    const bool condition = truthy(evaluate(*statement->expression,
                                                           environment, call_depth));
                    const Statement* branch = condition ? statement->then_branch.get()
                                                        : statement->else_branch.get();
                    if (branch != nullptr) execute_block(*branch, environment, call_depth);
                    continue;
                }
                if (statement->kind == Statement::Kind::block) {
                    execute_block(*statement, environment, call_depth);
                    continue;
                }
                if (statement->expression) {
                    (void)evaluate(*statement->expression, environment, call_depth);
                }
            }
        } catch (const ReturnSignal&) {
            for (const auto& name : declared) {
                environment.erase(name);
                release_immutable(name);
            }
            throw;
        } catch (const RuntimeFailure&) {
            for (const auto& name : declared) {
                environment.erase(name);
                release_immutable(name);
            }
            throw;
        }
        for (const auto& name : declared) {
            environment.erase(name);
            release_immutable(name);
        }
    }

    SimulationResult& result_;
    std::unordered_map<std::string, const Function*> functions_;
    std::unordered_map<std::string, std::size_t> immutable_;
};

const Attribute* attribute(const Function& function, const std::string& name) {
    for (const auto& item : function.attributes) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

void add_error(SimulationResult& result, std::string message, SourceLocation location) {
    result.diagnostics.push_back({DiagnosticSeverity::error, std::move(message), location});
}

} // namespace

SimulationResult Simulator::run(const Module& module, const SimulationOptions& options) const {
    SimulationResult result;
    const Function* entry = nullptr;
    for (const auto& function : module.functions) {
        if (attribute(function, "compute") == nullptr) continue;
        if (!options.entry_point.empty() && function.name != options.entry_point) continue;
        entry = &function;
        break;
    }
    if (entry == nullptr) {
        result.diagnostics.push_back({DiagnosticSeverity::error,
            options.entry_point.empty()
                ? "no compute entry point found"
                : "compute entry point '" + options.entry_point + "' was not found",
            {}});
        return result;
    }

    const Attribute* workgroup = attribute(*entry, "workgroup_size");
    if (workgroup == nullptr || workgroup->arguments.empty() ||
        workgroup->arguments.size() > 3) {
        add_error(result, "compute entry point has an invalid @workgroup_size",
                  entry->span.start);
        return result;
    }
    std::array<unsigned, 3> size{1, 1, 1};
    for (std::size_t i = 0; i < workgroup->arguments.size(); ++i) {
        if (!parse_integer(workgroup->arguments[i], size[i])) {
            add_error(result, "@workgroup_size must contain positive integer literals",
                      workgroup->span.start);
            return result;
        }
        if (size[i] > 256) {
            add_error(result, "a workgroup dimension cannot exceed 256",
                      workgroup->span.start);
            return result;
        }
    }
    const std::uint64_t workgroup_invocations =
        static_cast<std::uint64_t>(size[0]) * size[1] * size[2];
    if (workgroup_invocations > 256) {
        add_error(result, "total workgroup size cannot exceed 256 invocations",
                  workgroup->span.start);
        return result;
    }

    std::uint64_t total = 1;
    for (unsigned value : options.workgroups) {
        if (value == 0 || total > options.max_invocations / value) {
            add_error(result, "dispatch must be non-zero and within the invocation limit", {});
            return result;
        }
        total *= value;
    }
    for (unsigned value : size) {
        if (total > options.max_invocations / value) {
            add_error(result, "dispatch exceeds the simulator invocation limit", {});
            return result;
        }
        total *= value;
    }

    Interpreter interpreter(module, result);
    for (unsigned gz = 0; gz < options.workgroups[2]; ++gz) {
        for (unsigned gy = 0; gy < options.workgroups[1]; ++gy) {
            for (unsigned gx = 0; gx < options.workgroups[0]; ++gx) {
                for (unsigned lz = 0; lz < size[2]; ++lz) {
                    for (unsigned ly = 0; ly < size[1]; ++ly) {
                        for (unsigned lx = 0; lx < size[0]; ++lx) {
                            const std::array<unsigned, 3> group{gx, gy, gz};
                            const std::array<unsigned, 3> local{lx, ly, lz};
                            interpreter.invoke(*entry, group, local,
                                               options.workgroups, size);
                            if (has_errors(result.diagnostics)) return result;
                        }
                    }
                }
            }
        }
    }
    std::ostringstream summary;
    summary << "Executed " << total << " invocation(s) of '" << entry->name
            << "' (workgroup_size=" << size[0] << 'x' << size[1] << 'x' << size[2]
            << ", dispatch=" << options.workgroups[0] << 'x' << options.workgroups[1]
            << 'x' << options.workgroups[2] << ')';
    result.output.push_back(summary.str());
    return result;
}

} // namespace wgslcheck