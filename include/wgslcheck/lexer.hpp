#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "wgslcheck/diagnostic.hpp"

namespace wgslcheck {

/**
 * @brief All possible token types recognized by the WGSL Lexer.
 * 
 * This enum is exhaustive for the subset of WGSL that wgslcheck supports.
 * It includes keywords, punctuation, operators, and literals.
 */
enum class TokenKind {
    end_of_file,
    identifier,
    integer_literal,
    float_literal,
    left_paren,
    right_paren,
    left_brace,
    right_brace,
    left_bracket,
    right_bracket,
    comma,
    colon,
    semicolon,
    dot,
    at,
    plus,
    minus,
    star,
    slash,
    percent,
    ampersand,
    pipe,
    caret,
    bang,
    tilde,
    equal,
    less,
    greater,
    plus_equal,
    minus_equal,
    star_equal,
    slash_equal,
    percent_equal,
    ampersand_equal,
    pipe_equal,
    caret_equal,
    shift_left_equal,
    shift_right_equal,
    equal_equal,
    bang_equal,
    less_equal,
    greater_equal,
    shift_left,
    shift_right,
    arrow,
    and_and,
    or_or,
};

/**
 * @brief Represents a single lexical token produced by the Lexer.
 */
struct Token {
    TokenKind kind = TokenKind::end_of_file;
    
    /// The exact textual representation of the token from the source code.
    /// Using `std::string` here (rather than `string_view`) ensures the token 
    /// remains valid even if the original source buffer is moved or destroyed.
    std::string lexeme; 
    
    SourceSpan span; ///< Location of the token in the source code.
};

/**
 * @brief The Lexer (or Tokenizer) is responsible for breaking raw source code 
 * into a stream of `Token`s.
 * 
 * It is a hand-written, single-pass scanner. It advances character by character, 
 * skipping whitespace/comments (`skip_trivia`) and grouping characters into tokens.
 */
class Lexer {
public:
    /**
     * @brief Constructs a Lexer for a given source string.
     * @param source A string_view of the WGSL source code.
     */
    explicit Lexer(std::string_view source);

    /**
     * @brief Runs the scanner and returns all tokens.
     * 
     * It always appends an `end_of_file` token at the end to signal the parser.
     * 
     * @return A vector of parsed Tokens.
     */
    std::vector<Token> tokenize();

    /**
     * @brief Returns any lexical errors encountered during tokenization.
     * @return A const reference to the diagnostics vector.
     */
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    // --- Scanning State ---
    bool at_end() const noexcept;
    char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    
    // --- Tokenization Routines ---
    void skip_trivia();
    void scan_token();
    void scan_identifier();
    void scan_number();
    
    // --- Helpers ---
    void add_token(TokenKind kind);
    void report(std::string message, SourceLocation location);

    // --- Internal State ---
    std::string_view source_;       ///< The input source code (non-owning view).
    std::size_t offset_ = 0;        ///< Current absolute byte offset in `source_`.
    std::size_t line_ = 1;          ///< Current 1-based line number.
    std::size_t column_ = 1;        ///< Current 1-based column number.
    SourceLocation token_start_;    ///< The location where the current token began.
    
    std::vector<Token> tokens_;     ///< Output buffer of parsed tokens.
    std::vector<Diagnostic> diagnostics_; ///< Output buffer of lexical errors.
};

/**
 * @brief Utility function to convert a TokenKind to a human-readable string.
 * 
 * Extremely useful for debugging and for generating error messages 
 * (e.g., "expected ';' but got 'identifier'").
 */
const char* token_kind_name(TokenKind kind) noexcept;

} // namespace wgslcheck