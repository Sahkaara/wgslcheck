#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "wgslcheck/diagnostic.hpp"

namespace wgslcheck {

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

struct Token {
    TokenKind kind = TokenKind::end_of_file;
    std::string lexeme;
    SourceSpan span;
};

class Lexer {
public:
    explicit Lexer(std::string_view source);

    std::vector<Token> tokenize();
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    bool at_end() const noexcept;
    char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    void skip_trivia();
    void scan_token();
    void scan_identifier();
    void scan_number();
    void add_token(TokenKind kind);
    void report(std::string message, SourceLocation location);

    std::string_view source_;
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    SourceLocation token_start_;
    std::vector<Token> tokens_;
    std::vector<Diagnostic> diagnostics_;
};

const char* token_kind_name(TokenKind kind) noexcept;

} // namespace wgslcheck