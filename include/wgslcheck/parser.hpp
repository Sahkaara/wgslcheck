#pragma once

#include <string>
#include <vector>

#include "wgslcheck/ast.hpp"
#include "wgslcheck/lexer.hpp"

namespace wgslcheck {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Module parse();
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    const Token& current() const noexcept;
    const Token& previous() const noexcept;
    bool at_end() const noexcept;
    bool check(TokenKind kind) const noexcept;
    bool match(TokenKind kind);
    const Token& consume(TokenKind kind, const char* message);
    void synchronize();
    void parse_attributes(std::vector<Attribute>& attributes);
    std::vector<std::string> parse_attribute_arguments();
    Function parse_function(std::vector<Attribute> attributes);
    Parameter parse_parameter();
    std::string parse_type();
    std::unique_ptr<Statement> parse_block();
    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Expression> parse_expression();
    std::unique_ptr<Expression> parse_precedence(int minimum_precedence);
    std::unique_ptr<Expression> parse_prefix();
    int precedence(TokenKind kind) const noexcept;
    void report(std::string message, SourceLocation location);

    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace wgslcheck