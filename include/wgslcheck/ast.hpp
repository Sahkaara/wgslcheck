#pragma once

#include <memory>
#include <string>
#include <vector>

#include "wgslcheck/diagnostic.hpp"

namespace wgslcheck {

/**
 * @brief Represents a WGSL attribute (e.g., @compute, @workgroup_size(8, 1, 1)).
 * 
 * Attributes provide metadata to functions, variables, and parameters.
 * The name is the attribute identifier (e.g., "compute"), and arguments
 * hold any literal values passed inside the parentheses.
 */
struct Attribute {
    std::string name;
    std::vector<std::string> arguments;
    SourceSpan span;
};

/**
 * @brief Represents an expression in the AST.
 * 
 * In a language like WGSL, almost everything on the right side of an 
 * assignment is an expression. To handle complex nesting, the AST uses a 
 * tree structure where an expression can contain child expressions.
 */
struct Expression {
    enum class Kind {
        number,     ///< Integer or float literal.
        boolean,    ///< true or false.
        identifier, ///< Variable or function name.
        unary,      ///< Operator with one operand (e.g., -x, !x).
        binary,     ///< Operator with two operands (e.g., a + b).
        call,       ///< Function invocation (e.g., max(a, b)).
        member,     ///< Struct or vector field access (e.g., pos.x).
    };

    Kind kind = Kind::identifier;
    std::string text; ///< The lexical value (e.g., "42", "my_var", "+").
    SourceSpan span;
    
    /// Child expressions. For example, a `binary` expression will have 
    /// two children (left and right operands), while a `call` expression 
    /// will have one child for the function name and N children for arguments.
    std::vector<std::unique_ptr<Expression>> children;
};

/**
 * @brief Represents a single executable statement.
 * 
 * WGSL supports a subset of imperative programming constructs: blocks,
 * variable declarations, assignments, control flow (if), and returns.
 */
struct Statement {
    enum class Kind {
        block,            ///< A sequence of statements enclosed in braces { }.
        variable,         ///< Variable declaration (var or let).
        assignment,       ///< Assignment to a variable (e.g., x = 5).
        expression,       ///< An expression evaluated as a statement.
        return_statement, ///< Return from a function.
        if_statement,     ///< Conditional execution.
    };

    Kind kind = Kind::block;
    std::string name;         ///< Variable name (for `variable` kind).
    std::string type;         ///< Explicit type name (e.g., "u32").
    std::string operation;    ///< Assignment operator (e.g., "=", "+=").
    bool mutable_variable = false; ///< True if `var`, false if `let` (WGSL keywords).
    SourceSpan span;
    
    /// The primary expression evaluated by this statement (e.g., the RHS of an assignment).
    std::unique_ptr<Expression> expression;
    
    /// For `if_statement` blocks.
    std::unique_ptr<Statement> then_branch;
    std::unique_ptr<Statement> else_branch;
    
    /// For `block` statements, holds the list of inner statements.
    std::vector<std::unique_ptr<Statement>> statements;
};

/**
 * @brief Represents a function parameter.
 * 
 * Example: @builtin(global_invocation_id) gid: vec3<u32>
 */
struct Parameter {
    std::string name;
    std::string type;
    std::vector<Attribute> attributes;
    SourceSpan span;
};

/**
 * @brief Represents a top-level WGSL function definition.
 * 
 * Example: @compute @workgroup_size(8) fn main() { ... }
 */
struct Function {
    std::string name;
    std::string return_type; ///< Empty string if the function returns void.
    std::vector<Attribute> attributes;
    std::vector<Parameter> parameters;
    std::unique_ptr<Statement> body; ///< The root block statement of the function.
    SourceSpan span;
};

/**
 * @brief Represents a single member inside a WGSL struct.
 */
struct StructMember {
    std::string name;
    std::string type;
    std::vector<Attribute> attributes;
    SourceSpan span;
};

/**
 * @brief Represents a WGSL struct definition.
 */
struct Struct {
    std::string name;
    std::vector<StructMember> members;
    SourceSpan span;
};

/**
 * @brief The root of the AST representing an entire WGSL module.
 */
struct Module {
    std::vector<Function> functions;
    std::vector<Struct> structs; ///< NEW: List of user-defined structs.
};

} // namespace wgslcheck