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

/**
 * @brief Represents a runtime value in the simulator.
 * WGSL has a complex type system (scalars, vectors, matrices), but for
 * a CPU debugger, a simplified tagged union is sufficient.
 */
struct Value {
    enum class Numeric { abstract_integer, abstract_float, u32, i32, f32 };
    Numeric numeric = Numeric::abstract_integer;
    enum class Kind { number, boolean, vector };
    Kind kind = Kind::number;
    double number = 0.0;
    bool boolean = false;
    std::vector<double> components; ///< Used for vec2, vec3, vec4

    static Value scalar(double value, Numeric type = Numeric::abstract_integer) {
        Value result;
        result.number = value;
        result.numeric = type;
        return result;
    }

    static Value boolean_value(bool value) {
        Value result;
        result.kind = Kind::boolean;
        result.boolean = value;
        return result;
    }

    static Value vector_value(std::vector<double> values, Numeric type = Numeric::abstract_integer) {
        Value result;
        result.kind = Kind::vector;
        result.components = std::move(values);
        result.numeric = type;
        return result;
    }
};

/**
 * @brief Exception used to unwind the stack when a `return` statement is hit.
 * This is a common technique in tree-walking interpreters to handle control flow.
 */
struct ReturnSignal {
    Value value;
};

// Mutability belongs to a function invocation, just like its local values.
// Restore the caller's bindings even when execution unwinds through an error.
class ImmutableFrame {
public:
    using Bindings = std::unordered_map<std::string, std::size_t>;

    explicit ImmutableFrame(Bindings& bindings) : bindings_(bindings) {
        saved_.swap(bindings_);
    }
    ~ImmutableFrame() { saved_.swap(bindings_); }
    ImmutableFrame(const ImmutableFrame&) = delete;
    ImmutableFrame& operator=(const ImmutableFrame&) = delete;

private:
    Bindings& bindings_;
    Bindings saved_;
};

/**
 * @brief Parses an integer literal from a string, respecting WGSL suffixes.
 */
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

/**
 * @brief Evaluates the "truthiness" of a Value.
 */
bool truthy(const Value& value) {
    if (value.kind == Value::Kind::boolean) return value.boolean;
    if (value.kind == Value::Kind::number) return value.number != 0.0;
    return !value.components.empty();
}

/**
 * @brief The core tree-walking interpreter for WGSL compute shaders.
 */
class Interpreter {
public:
    Interpreter(const Module& module, SimulationResult& result)
        : result_(result) {
        for (const auto& function : module.functions) {
            functions_.emplace(function.name, &function);
        }
    }

    /**
     * @brief Invokes a single shader function for a given global/local ID.
     * This is called in a loop for every invocation in the dispatch grid.
     */
    void invoke(const Function& function, const std::array<unsigned, 3>& group,
                const std::array<unsigned, 3>& local,
                const std::array<unsigned, 3>& dispatch,
                const std::array<unsigned, 3>& workgroup_size) {
        
        std::unordered_map<std::string, Value> environment;
        ImmutableFrame frame(immutable_);
        
        // Calculate built-in IDs
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
            
            // Bind parameters (e.g., @builtin(global_invocation_id))
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
                    environment.emplace(parameter.name, Value::scalar(linear, Value::Numeric::u32));
                } else {
                    fail("unsupported compute builtin '@builtin(" + name + ")'",
                         builtin->span.start);
                }
            }

            execute_block(*function.body, environment, 0);
        } catch (const ReturnSignal&) {
            // Normal return from entry point: ignore value.
        } catch (const RuntimeFailure&) {
            // Error already recorded in result_.diagnostics.
            return;
        }
    }

private:
    /**
     * @brief Internal exception used to halt execution upon a fatal runtime error.
     */
    struct RuntimeFailure {};

    static Value vector(const std::array<unsigned, 3>& values) {
        return Value::vector_value({static_cast<double>(values[0]),
                                    static_cast<double>(values[1]),
                                    static_cast<double>(values[2])}, Value::Numeric::u32);
    }

    [[noreturn]] void fail(const std::string& message, SourceLocation location) {
        result_.diagnostics.push_back({DiagnosticSeverity::error, message, location});
        throw RuntimeFailure{};
    }

    double number(const Value& value, SourceLocation location) {
        if (value.kind != Value::Kind::number) fail("expected a scalar number", location);
        return value.number;
    }

    Value convert(const Value& value, const std::string& type, SourceLocation location) {
        if (type == "f16") fail("f16 simulation is not supported", location);
        if (type == "bool") {
            if (value.kind == Value::Kind::vector) fail("expected scalar conversion argument", location);
            return Value::boolean_value(truthy(value));
        }
        double n = value.kind == Value::Kind::boolean ? (value.boolean ? 1.0 : 0.0) : number(value, location);
        if (!std::isfinite(n)) fail("non-finite numeric conversion", location);
        if (type == "f32") {
            if (std::abs(n) > std::numeric_limits<float>::max()) fail("f32 conversion overflow", location);
            return Value::scalar(static_cast<float>(n), Value::Numeric::f32);
        }
        const bool unsigned_target = type == "u32";
        const auto target = unsigned_target ? Value::Numeric::u32 : Value::Numeric::i32;
        if (value.numeric == Value::Numeric::u32 || value.numeric == Value::Numeric::i32) {
            const auto bits = static_cast<std::uint32_t>(static_cast<std::int64_t>(n));
            return Value::scalar(!unsigned_target && bits >= 0x80000000u
                ? static_cast<double>(bits) - 4294967296.0 : static_cast<double>(bits), target);
        }
        const double low = unsigned_target ? 0.0 : -2147483648.0;
        double high = unsigned_target ? 4294967295.0 : 2147483647.0;
        if (value.numeric == Value::Numeric::abstract_integer && (n < low || n > high))
            fail("abstract integer conversion out of range", location);
        if (value.numeric == Value::Numeric::f32)
            high = unsigned_target ? 4294967040.0 : 2147483520.0;
        n = std::trunc(std::clamp(n, low, high));
        return Value::scalar(n, target);
    }

    Value binary(const std::string& op, const Value& left, const Value& right, SourceLocation location) {
        if ((op == "==" || op == "!=") &&
            left.kind == Value::Kind::boolean && right.kind == Value::Kind::boolean) {
            const bool equal = left.boolean == right.boolean;
            return Value::boolean_value(op == "==" ? equal : !equal);
        }
        double a = number(left, location), b = number(right, location);
        using N = Value::Numeric;
        const bool shift = op == "<<" || op == ">>";
        auto type = left.numeric;
        if (!shift) {
            if (type == N::abstract_integer || type == N::abstract_float) {
                if (right.numeric != N::abstract_integer) type = right.numeric;
            } else if (right.numeric != N::abstract_integer && right.numeric != N::abstract_float &&
                       right.numeric != type) {
                fail("mixed concrete numeric types require an explicit conversion", location);
            }
        }
        if (type == N::f32) {
            a = convert(left, "f32", location).number;
            b = convert(right, "f32", location).number;
        }
        if (type == N::u32 || type == N::i32) {
            const auto promote = [&](const Value& operand) {
                if (operand.numeric == N::abstract_float)
                    fail("floating-point operand requires explicit integer conversion", location);
                return operand.numeric == N::abstract_integer
                    ? convert(operand, type == N::u32 ? "u32" : "i32", location).number
                    : operand.number;
            };
            a = promote(left);
            if (!shift) b = promote(right);
        }
        if (op == "<") return Value::boolean_value(a < b);
        if (op == "==") return Value::boolean_value(a == b);
        if (op == "!=") return Value::boolean_value(a != b);
        if (op == "<=") return Value::boolean_value(a <= b);
        if (op == ">") return Value::boolean_value(a > b);
        if (op == ">=") return Value::boolean_value(a >= b);
        const bool integral = type == N::u32 || type == N::i32 || type == N::abstract_integer;
        if (integral && (std::trunc(a) != a || std::trunc(b) != b))
            fail("integer operation requires integer operands", location);
        const auto finish = [&](double n) {
            if (!std::isfinite(n)) fail("non-finite arithmetic result", location);
            if (type == N::u32 || type == N::i32) {
                n = std::fmod(n, 4294967296.0);
                if (n < 0) n += 4294967296.0;
                if (type == N::i32 && n >= 2147483648.0) n -= 4294967296.0;
            } else if (type == N::f32) return convert(Value::scalar(n), "f32", location);
            else if (integral && std::abs(n) > 9007199254740991.0)
                fail("abstract integer exceeds simulator exact range", location);
            return Value::scalar(n, type);
        };
        if (op == "+") return finish(a + b);
        if (op == "-") return finish(a - b);
        if (op == "*") {
            if (type == N::u32 || type == N::i32) {
                const auto x = static_cast<std::uint32_t>(integer(a, location));
                const auto y = static_cast<std::uint32_t>(integer(b, location));
                return finish(static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * y));
            }
            return finish(a * b);
        }
        if (op == "/" || op == "%") {
            if (b == 0) fail("division or remainder by zero", location);
            return finish(op == "%" ? std::fmod(a, b) : integral ? std::trunc(a / b) : a / b);
        }
        if (!integral) fail("bitwise operations require integers", location);
        const auto x = integer(a, location), y = integer(b, location);
        if (op == "&") return finish(static_cast<double>(x & y));
        if (op == "|") return finish(static_cast<double>(x | y));
        if (op == "^") return finish(static_cast<double>(x ^ y));
        if (shift) {
            if (type == N::u32 || type == N::i32) {
                if (y < 0 || y > 4294967295LL) fail("invalid shift amount", location);
                const unsigned amount = static_cast<unsigned>(y) % 32;
                if (op == ">>" && type == N::i32) return finish(static_cast<double>(x >> amount));
                const auto bits = static_cast<std::uint32_t>(x);
                return finish(op == "<<" ? static_cast<std::uint32_t>(bits << amount) : bits >> amount);
            }
            if (y < 0 || y >= 64) fail("shift amount must be between 0 and 63", location);
            if (op == ">>") return finish(static_cast<double>(x >> y));
            return finish(a * std::pow(2.0, static_cast<double>(y)));
        }
        fail("unsupported binary operator '" + op + "'", location);
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

    /**
     * @brief Evaluates an expression to a runtime Value.
     */
    Value evaluate(const Expression& expression,
                   std::unordered_map<std::string, Value>& environment,
                   unsigned call_depth) {
        switch (expression.kind) {
        case Expression::Kind::number: {
            std::string literal = expression.text;
            // In hexadecimal integers, a trailing 'f' is a digit, not a suffix.
            const bool hexadecimal = literal.size() > 2 && literal[0] == '0' &&
                (literal[1] == 'x' || literal[1] == 'X');
            if (!literal.empty() && (literal.back() == 'u' || literal.back() == 'i' ||
                (!hexadecimal && (literal.back() == 'f' || literal.back() == 'h')))) {
                literal.pop_back();
            }
            try {
                std::size_t consumed = 0;
                const double value = std::stod(literal, &consumed);
                if (consumed != literal.size() || !std::isfinite(value)) {
                    fail("invalid numeric literal for simulator", expression.span.start);
                }
                const char suffix = expression.text.back();
                const bool floating = !hexadecimal && expression.text.find_first_of(".eEfh") != std::string::npos;
                Value parsed = Value::scalar(value, floating ? Value::Numeric::abstract_float : Value::Numeric::abstract_integer);
                if (suffix == 'u' || suffix == 'i') {
                    if (value > (suffix == 'u' ? 4294967295.0 : 2147483647.0))
                        fail("integer literal out of range", expression.span.start);
                    return convert(parsed, suffix == 'u' ? "u32" : "i32", expression.span.start);
                }
                if (!hexadecimal && suffix == 'f') return convert(parsed, "f32", expression.span.start);
                if (!hexadecimal && suffix == 'h') fail("f16 simulation is not supported", expression.span.start);
                if (!floating && value > 9007199254740991.0)
                    fail("abstract integer exceeds simulator exact range", expression.span.start);
                return parsed;
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
            // Support both spatial (x,y,z) and color (r,g,b) swizzles
            const std::string_view member = expression.text;
            const std::size_t index = member == "x" || member == "r" ? 0 :
                                      member == "y" || member == "g" ? 1 :
                                      member == "z" || member == "b" ? 2 :
                                      member == "w" || member == "a" ? 3 :
                                      std::numeric_limits<std::size_t>::max();
            if (index >= base.components.size()) {
                fail("invalid vector component '" + expression.text + "'", expression.span.start);
            }
            return Value::scalar(base.components[index], base.numeric);
        }
        case Expression::Kind::unary: {
            const Value operand = evaluate(*expression.children[0], environment, call_depth);
            if (expression.text == "!") return Value::boolean_value(!truthy(operand));
            (void)number(operand, expression.span.start);
            if (expression.text == "-") return binary("-", Value::scalar(0, operand.numeric), operand, expression.span.start);
            if (expression.text == "+") return operand;
            if (expression.text == "~") {
                return binary("^", operand, Value::scalar(-1, operand.numeric), expression.span.start);
            }
            fail("unsupported unary operator", expression.span.start);
        }
        case Expression::Kind::binary: {
            const Value left = evaluate(*expression.children[0], environment, call_depth);
            
            // Short-circuit evaluation for logical operators
            if (expression.text == "&&" && !truthy(left)) return Value::boolean_value(false);
            if (expression.text == "||" && truthy(left)) return Value::boolean_value(true);
            
            const Value right = evaluate(*expression.children[1], environment, call_depth);
            if (expression.text == "&&") return Value::boolean_value(truthy(right));
            if (expression.text == "||") return Value::boolean_value(truthy(right));
            
            return binary(expression.text, left, right, expression.span.start);
        }

        case Expression::Kind::call:
            return call(expression, environment, call_depth);
        }
        fail("unsupported expression", expression.span.start);
    }

    /**
     * @brief Handles function calls (both built-ins and user-defined functions).
     */
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
        
        // --- 1. Built-in type constructors ---
        if (name == "u32" || name == "i32" || name == "f32" ||
            name == "f16" || name == "bool") {
            if (arguments.size() > 1) fail("scalar constructor expects zero or one argument", expression.span.start);
            return convert(arguments.empty() ? Value::scalar(0) : arguments[0], name, expression.span.start);
        }
        if (name.rfind("vec", 0) == 0 && name.size() == 4 &&
            name[3] >= '2' && name[3] <= '4') {
            const std::size_t width = static_cast<std::size_t>(name[3] - '0');
            std::vector<double> components;
            auto component_type = arguments.empty() ? Value::Numeric::abstract_integer : arguments.front().numeric;
            for (const auto& argument : arguments) {
                if (argument.numeric != component_type)
                    fail("mixed vector component types are not supported", expression.span.start);
                if (argument.kind == Value::Kind::vector) {
                    components.insert(components.end(), argument.components.begin(),
                                      argument.components.end());
                } else {
                    components.push_back(number(argument, expression.span.start));
                }
            }
            // Splat behavior: vec3(1.0) -> vec3<f32>(1.0, 1.0, 1.0)
            if (components.empty()) components.resize(width, 0);
            if (components.size() == 1) components.resize(width, components.front());
            if (components.size() != width) {
                fail("vector constructor has the wrong number of components",
                     expression.span.start);
            }
            return Value::vector_value(std::move(components), component_type);
        }
        
        // --- 2. Built-in math functions ---
        if (name == "min" || name == "max") {
            if (arguments.size() != 2) fail(name + " expects two arguments", expression.span.start);
            const double a = number(arguments[0], expression.span.start);
            const double b = number(arguments[1], expression.span.start);
            // Select through arithmetic promotion so concrete types are retained.
            const auto selected = name == "min" ? (a < b ? arguments[0] : arguments[1])
                                                 : (a > b ? arguments[0] : arguments[1]);
            const auto other = name == "min" ? (a < b ? arguments[1] : arguments[0])
                                              : (a > b ? arguments[1] : arguments[0]);
            return binary("+", selected, Value::scalar(0, other.numeric), expression.span.start);
        }
        if (name == "abs" || name == "sqrt" || name == "floor" || name == "ceil") {
            if (arguments.size() != 1) fail(name + " expects one argument", expression.span.start);
            const double n = number(arguments[0], expression.span.start);
            if (name == "sqrt" && n < 0.0) fail("sqrt argument cannot be negative", expression.span.start);
            if (name == "abs") return n < 0 ? binary("-", Value::scalar(0, arguments[0].numeric), arguments[0], expression.span.start) : arguments[0];
            if (arguments[0].numeric != Value::Numeric::abstract_float && arguments[0].numeric != Value::Numeric::f32)
                fail(name + " requires a floating-point argument", expression.span.start);
            const auto value = Value::scalar(name == "sqrt" ? std::sqrt(n) : name == "floor" ? std::floor(n) : std::ceil(n), arguments[0].numeric);
            return value.numeric == Value::Numeric::f32 ? convert(value, "f32", expression.span.start) : value;
        }

        // --- 3. User-defined functions ---
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
        ImmutableFrame frame(immutable_);
        std::vector<std::string> immutable_parameters;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& type = target.parameters[i].type;
            locals.emplace(target.parameters[i].name,
                type == "u32" || type == "i32" || type == "f32" || type == "f16" || type == "bool"
                    ? convert(arguments[i], type, target.parameters[i].span.start) : arguments[i]);
            mark_immutable(target.parameters[i].name);
            immutable_parameters.push_back(target.parameters[i].name);
        }
        
        try {
            execute_block(*target.body, locals, call_depth + 1);
        } catch (const ReturnSignal& returned) {
            for (const auto& parameter : immutable_parameters) release_immutable(parameter);
            const auto& type = target.return_type;
            return type == "u32" || type == "i32" || type == "f32" || type == "f16" || type == "bool"
                ? convert(returned.value, type, target.span.start) : returned.value;
        } catch (const RuntimeFailure&) {
            for (const auto& parameter : immutable_parameters) release_immutable(parameter);
            throw;
        }
        for (const auto& parameter : immutable_parameters) release_immutable(parameter);
        return Value::scalar(0.0); // Default return if no `return` statement
    }

    /**
     * @brief Executes a block of statements.
     * Handles variable scoping by tracking declarations and cleaning up 
     * the environment when the block exits (including via exceptions).
     */
    void execute_block(const Statement& block,
                       std::unordered_map<std::string, Value>& environment,
                       unsigned call_depth) {
        std::vector<std::string> declared;
        std::unordered_map<std::string, Value> shadowed;
        std::unordered_map<std::string, std::size_t> shadowed_immutable;
        const auto restore_scope = [&] {
            for (const auto& name : declared) {
                const auto previous = shadowed.find(name);
                if (previous != shadowed.end()) {
                    environment.insert_or_assign(name, std::move(previous->second));
                } else {
                    environment.erase(name);
                }
                immutable_.erase(name);
                const auto previous_immutable = shadowed_immutable.find(name);
                if (previous_immutable != shadowed_immutable.end()) {
                    immutable_[name] = previous_immutable->second;
                }
            }
        };
        try {
            for (const auto& statement : block.statements) {
                // 1. Variable declarations
                if (statement->kind == Statement::Kind::variable) {
                    if (std::find(declared.begin(), declared.end(), statement->name) !=
                        declared.end()) {
                        fail("variable '" + statement->name + "' is already declared",
                             statement->span.start);
                    }
                    if (statement->expression == nullptr) {
                        fail("simulator does not support uninitialized variables",
                             statement->span.start);
                    }
                    // Evaluate before replacing the binding: the initializer may
                    // refer to a variable of the same name in the enclosing scope.
                    Value initial = evaluate(*statement->expression, environment, call_depth);
                    const auto& type = statement->type;
                    if (type == "u32" || type == "i32" || type == "f32" || type == "f16" || type == "bool")
                        initial = convert(initial, type, statement->span.start);
                    const auto previous = environment.find(statement->name);
                    if (previous != environment.end()) {
                        shadowed.emplace(statement->name, std::move(previous->second));
                    }
                    const auto previous_immutable = immutable_.find(statement->name);
                    if (previous_immutable != immutable_.end()) {
                        shadowed_immutable.emplace(statement->name, previous_immutable->second);
                        immutable_.erase(previous_immutable);
                    }
                    environment.insert_or_assign(statement->name, std::move(initial));
                    declared.push_back(statement->name);
                    if (!statement->mutable_variable) mark_immutable(statement->name);
                    continue;
                }
                
                // 2. Assignments
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
                        const auto type = target->second.numeric;
                        target->second = target->second.kind == Value::Kind::number &&
                            (type == Value::Numeric::u32 || type == Value::Numeric::i32 || type == Value::Numeric::f32)
                            ? convert(right, type == Value::Numeric::u32 ? "u32" : type == Value::Numeric::i32 ? "i32" : "f32", statement->span.start)
                            : right;
                    } else {
                        target->second = binary(statement->operation.substr(0, statement->operation.size() - 1),
                                                target->second, right, statement->span.start);
                    }

                    continue;
                }
                
                // 3. Return statements
                if (statement->kind == Statement::Kind::return_statement) {
                    throw ReturnSignal{statement->expression
                        ? evaluate(*statement->expression, environment, call_depth)
                        : Value::scalar(0.0)};
                }
                
                // 4. If statements
                if (statement->kind == Statement::Kind::if_statement) {
                    const Statement* branch = statement.get();
                    // An else-if is another conditional node, not a block.
                    while (branch != nullptr &&
                           branch->kind == Statement::Kind::if_statement) {
                        const bool condition = truthy(evaluate(*branch->expression,
                                                               environment, call_depth));
                        branch = condition ? branch->then_branch.get()
                                           : branch->else_branch.get();
                    }
                    if (branch != nullptr) execute_block(*branch, environment, call_depth);
                    continue;
                }
                
                // 5. Nested blocks
                if (statement->kind == Statement::Kind::block) {
                    execute_block(*statement, environment, call_depth);
                    continue;
                }
                
                // 6. Expression statements (e.g., a function call)
                if (statement->expression) {
                    (void)evaluate(*statement->expression, environment, call_depth);
                }
            }
        } catch (const ReturnSignal&) {
            // Cleanup scope before propagating the return signal
            restore_scope();
            throw;
        } catch (const RuntimeFailure&) {
            // Cleanup scope before propagating the runtime error
            restore_scope();
            throw;
        }
        
        // Cleanup scope on normal block exit
        restore_scope();
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

/**
 * @brief Main simulation driver.
 * Validates workgroup sizes and limits, then loops over the 3D dispatch grid.
 */
SimulationResult Simulator::run(const Module& module, const SimulationOptions& options) const {
    SimulationResult result;
    
    // 1. Find the compute entry point
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

    // 2. Parse and validate @workgroup_size
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

    // 3. Validate dispatch grid and total invocations limit
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

    // 4. Execute the shader
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
                            // Stop execution immediately if a runtime error occurred
                            if (has_errors(result.diagnostics)) return result;
                        }
                    }
                }
            }
        }
    }
    
    // 5. Generate summary output
    std::ostringstream summary;
    summary << "Executed " << total << " invocation(s) of '" << entry->name
            << "' (workgroup_size=" << size[0] << 'x' << size[1] << 'x' << size[2]
            << ", dispatch=" << options.workgroups[0] << 'x' << options.workgroups[1]
            << 'x' << options.workgroups[2] << ')';
    result.output.push_back(summary.str());
    return result;
}

} // namespace wgslcheck
