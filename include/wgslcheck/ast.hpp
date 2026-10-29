#pragma once

#include <memory>
#include <string>
#include <vector>

#include "wgslcheck/diagnostic.hpp"

namespace wgslcheck {

struct Attribute {
    std::string name;
    std::vector<std::string> arguments;
    SourceSpan span;
};

struct Expression {
    enum class Kind {
        number,
        boolean,
        identifier,
        unary,
        binary,
        call,
        member,
    };

    Kind kind = Kind::identifier;
    std::string text;
    SourceSpan span;
    std::vector<std::unique_ptr<Expression>> children;
};

struct Statement {
    enum class Kind {
        block,
        variable,
        assignment,
        expression,
        return_statement,
        if_statement,
    };

    Kind kind = Kind::block;
    std::string name;
    std::string type;
    std::string operation;
    bool mutable_variable = false;
    SourceSpan span;
    std::unique_ptr<Expression> expression;
    std::unique_ptr<Statement> then_branch;
    std::unique_ptr<Statement> else_branch;
    std::vector<std::unique_ptr<Statement>> statements;
};

struct Parameter {
    std::string name;
    std::string type;
    std::vector<Attribute> attributes;
    SourceSpan span;
};

struct Function {
    std::string name;
    std::string return_type;
    std::vector<Attribute> attributes;
    std::vector<Parameter> parameters;
    std::unique_ptr<Statement> body;
    SourceSpan span;
};

struct Module {
    std::vector<Function> functions;
};

} // namespace wgslcheck