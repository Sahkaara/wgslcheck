#pragma once

#include <string>
#include <vector>

#include "wgslcheck/ast.hpp"
#include "wgslcheck/lexer.hpp"

namespace wgslcheck {

/**
 * @brief The Parser consumes a vector of tokens and builds an Abstract Syntax Tree (AST).
 * 
 * This is a hand-written, recursive descent parser. It uses one-token lookahead
 * and implements a Pratt-parsing algorithm for expressions (parse_precedence) 
 * to correctly handle operator precedence (e.g., multiplication before addition).
 */
class Parser {
public:
    /**
     * @brief Constructs a Parser from a pre-tokenized stream.
     * @param tokens A vector of tokens produced by the Lexer.
     */
    explicit Parser(std::vector<Token> tokens);

    /**
     * @brief Parses the token stream into a Module AST.
     * @return The root `Module` node. Check `diagnostics()` to see if errors occurred.
     */
    Module parse();

    /**
     * @brief Returns any syntax errors encountered during parsing.
     */
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    // --- Token Navigation Helpers ---
    const Token& current() const noexcept;
    const Token& previous() const noexcept;
    bool at_end() const noexcept;
    bool check(TokenKind kind) const noexcept;
    bool match(TokenKind kind);
    const Token& consume(TokenKind kind, const char* message);
    
    /**
     * @brief Error recovery mechanism.
     * When a syntax error occurs, this function skips tokens until it finds
     * a safe synchronization point (e.g., a semicolon or a keyword like `fn`).
     * This prevents a single typo from generating a cascade of false errors.
     */
    void synchronize();

    // --- Grammar Rules ---
    void parse_attributes(std::vector<Attribute>& attributes);
    std::vector<std::string> parse_attribute_arguments();
    Function parse_function(std::vector<Attribute> attributes);
    // --- NEW: Struct Parsing ---
    Struct parse_struct();
    StructMember parse_struct_member();
    Parameter parse_parameter();
    std::string parse_type();
    std::unique_ptr<Statement> parse_block();
    std::unique_ptr<Statement> parse_statement();
    
    // --- Expression Parsing (Pratt Parser) ---
    std::unique_ptr<Expression> parse_expression();
    std::unique_ptr<Expression> parse_precedence(int minimum_precedence);
    std::unique_ptr<Expression> parse_prefix();
    
    /**
     * @brief Returns the binding power (precedence) of an operator.
     * Higher numbers bind tighter. Returns -1 if the token is not an operator.
     */
    int precedence(TokenKind kind) const noexcept;

    // --- Error Reporting ---
    void report(std::string message, SourceLocation location);

    // --- Internal State ---
    std::vector<Token> tokens_;    ///< The input token stream.
    std::size_t current_ = 0;      ///< Index of the current token being parsed.
    std::vector<Diagnostic> diagnostics_; ///< Accumulates syntax errors.
};

} // namespace wgslcheck