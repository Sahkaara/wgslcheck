#include "wgslcheck/lexer.hpp"

#include <cctype>
#include <utility>

namespace wgslcheck {
namespace {

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_continue(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

std::vector<Token> Lexer::tokenize() {
    while (!at_end()) {
        skip_trivia();
        if (at_end()) {
            break;
        }
        token_start_ = {offset_, line_, column_};
        scan_token();
    }

    SourceLocation end{offset_, line_, column_};
    tokens_.push_back({TokenKind::end_of_file, {}, {end, end}});
    return std::move(tokens_);
}

const std::vector<Diagnostic>& Lexer::diagnostics() const noexcept {
    return diagnostics_;
}

bool Lexer::at_end() const noexcept {
    return offset_ >= source_.size();
}

char Lexer::peek(std::size_t lookahead) const noexcept {
    const std::size_t index = offset_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() noexcept {
    if (at_end()) {
        return '\0';
    }
    const char c = source_[offset_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) noexcept {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skip_trivia() {
    for (;;) {
        if (peek() == ' ' || peek() == '\r' || peek() == '\t' || peek() == '\n') {
            advance();
            continue;
        }
        if (peek() == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (peek() == '/' && peek(1) == '*') {
            const SourceLocation start{offset_, line_, column_};
            advance();
            advance();
            std::size_t depth = 1;
            while (!at_end() && depth != 0) {
                if (peek() == '/' && peek(1) == '*') {
                    advance();
                    advance();
                    ++depth;
                } else if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    --depth;
                } else {
                    advance();
                }
            }
            if (depth != 0) {
                report("unterminated block comment", start);
            }
            continue;
        }
        return;
    }
}

void Lexer::scan_token() {
    const char c = advance();
    if (is_identifier_start(c)) {
        scan_identifier();
        return;
    }
    if (c >= '0' && c <= '9') {
        scan_number();
        return;
    }

    switch (c) {
    case '(': add_token(TokenKind::left_paren); break;
    case ')': add_token(TokenKind::right_paren); break;
    case '{': add_token(TokenKind::left_brace); break;
    case '}': add_token(TokenKind::right_brace); break;
    case '[': add_token(TokenKind::left_bracket); break;
    case ']': add_token(TokenKind::right_bracket); break;
    case ',': add_token(TokenKind::comma); break;
    case ':': add_token(TokenKind::colon); break;
    case ';': add_token(TokenKind::semicolon); break;
    case '.': add_token(TokenKind::dot); break;
    case '@': add_token(TokenKind::at); break;
    case '+': add_token(match('=') ? TokenKind::plus_equal : TokenKind::plus); break;
    case '-':
        if (match('>')) add_token(TokenKind::arrow);
        else add_token(match('=') ? TokenKind::minus_equal : TokenKind::minus);
        break;
    case '*': add_token(match('=') ? TokenKind::star_equal : TokenKind::star); break;
    case '/': add_token(match('=') ? TokenKind::slash_equal : TokenKind::slash); break;
    case '%': add_token(match('=') ? TokenKind::percent_equal : TokenKind::percent); break;
    case '&':
        if (match('&')) add_token(TokenKind::and_and);
        else add_token(match('=') ? TokenKind::ampersand_equal : TokenKind::ampersand);
        break;
    case '|':
        if (match('|')) add_token(TokenKind::or_or);
        else add_token(match('=') ? TokenKind::pipe_equal : TokenKind::pipe);
        break;
    case '^': add_token(match('=') ? TokenKind::caret_equal : TokenKind::caret); break;
    case '!': add_token(match('=') ? TokenKind::bang_equal : TokenKind::bang); break;
    case '~': add_token(TokenKind::tilde); break;
    case '=': add_token(match('=') ? TokenKind::equal_equal : TokenKind::equal); break;
    case '<':
        if (match('<')) add_token(match('=') ? TokenKind::shift_left_equal : TokenKind::shift_left);
        else add_token(match('=') ? TokenKind::less_equal : TokenKind::less);
        break;
    case '>':
        if (match('>')) add_token(match('=') ? TokenKind::shift_right_equal : TokenKind::shift_right);
        else add_token(match('=') ? TokenKind::greater_equal : TokenKind::greater);
        break;
    default:
        report("unexpected character", token_start_);
        break;
    }
}

void Lexer::scan_identifier() {
    while (is_identifier_continue(peek())) {
        advance();
    }
    add_token(TokenKind::identifier);
}

void Lexer::scan_number() {
    bool floating = false;
    const bool hexadecimal = source_[token_start_.offset] == '0' &&
                             (peek() == 'x' || peek() == 'X');
    if (hexadecimal) {
        advance();
        const std::size_t digits_start = offset_;
        while (is_hex_digit(peek())) {
            advance();
        }
        if (digits_start == offset_) {
            report("expected hexadecimal digits after 0x", token_start_);
        }
        while (peek() == 'i' || peek() == 'u') {
            advance();
        }
    } else {
        while (peek() >= '0' && peek() <= '9') {
            advance();
        }
        if (peek() == '.' && peek(1) >= '0' && peek(1) <= '9') {
            floating = true;
            advance();
            while (peek() >= '0' && peek() <= '9') {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            floating = true;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            const std::size_t exponent_start = offset_;
            while (peek() >= '0' && peek() <= '9') {
                advance();
            }
            if (exponent_start == offset_) {
                report("expected digits in numeric exponent", token_start_);
            }
        }
        if (peek() == 'f' || peek() == 'h') {
            floating = true;
            advance();
        } else if (peek() == 'i' || peek() == 'u') {
            advance();
        }
    }
    add_token(floating ? TokenKind::float_literal : TokenKind::integer_literal);
}

void Lexer::add_token(TokenKind kind) {
    const SourceLocation end{offset_, line_, column_};
    tokens_.push_back({kind,
        std::string(source_.substr(token_start_.offset, offset_ - token_start_.offset)),
        {token_start_, end}});
}

void Lexer::report(std::string message, SourceLocation location) {
    diagnostics_.push_back({DiagnosticSeverity::error, std::move(message), location});
}

const char* token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::end_of_file: return "end of file";
    case TokenKind::identifier: return "identifier";
    case TokenKind::integer_literal: return "integer literal";
    case TokenKind::float_literal: return "float literal";
    case TokenKind::left_paren: return "'('";
    case TokenKind::right_paren: return "')'";
    case TokenKind::left_brace: return "'{'";
    case TokenKind::right_brace: return "'}'";
    case TokenKind::left_bracket: return "'['";
    case TokenKind::right_bracket: return "']'";
    case TokenKind::comma: return "','";
    case TokenKind::colon: return "':'";
    case TokenKind::semicolon: return "';'";
    case TokenKind::dot: return "'.'";
    case TokenKind::at: return "'@'";
    case TokenKind::plus: return "'+'";
    case TokenKind::minus: return "'-'";
    case TokenKind::star: return "'*'";
    case TokenKind::slash: return "'/'";
    case TokenKind::percent: return "'%'";
    case TokenKind::ampersand: return "'&'";
    case TokenKind::pipe: return "'|'";
    case TokenKind::caret: return "'^'";
    case TokenKind::bang: return "'!'";
    case TokenKind::tilde: return "'~'";
    case TokenKind::equal: return "'='";
    case TokenKind::less: return "'<'";
    case TokenKind::greater: return "'>'";
    case TokenKind::plus_equal: return "'+='";
    case TokenKind::minus_equal: return "'-='";
    case TokenKind::star_equal: return "'*='";
    case TokenKind::slash_equal: return "'/='";
    case TokenKind::percent_equal: return "'%='";
    case TokenKind::ampersand_equal: return "'&='";
    case TokenKind::pipe_equal: return "'|='";
    case TokenKind::caret_equal: return "'^='";
    case TokenKind::shift_left_equal: return "'<<='";
    case TokenKind::shift_right_equal: return "'>>='";
    case TokenKind::equal_equal: return "'=='";
    case TokenKind::bang_equal: return "'!='";
    case TokenKind::less_equal: return "'<='";
    case TokenKind::greater_equal: return "'>='";
    case TokenKind::shift_left: return "'<<'";
    case TokenKind::shift_right: return "'>>'";
    case TokenKind::arrow: return "'->'";
    case TokenKind::and_and: return "'&&'";
    case TokenKind::or_or: return "'||'";
    }
    return "token";
}

} // namespace wgslcheck