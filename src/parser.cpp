#include "wgslcheck/parser.hpp"

#include <stdexcept>
#include <utility>

namespace wgslcheck {
namespace {

struct ParseFailure {};

bool is_assignment(TokenKind kind) {
    return kind == TokenKind::equal || kind == TokenKind::plus_equal ||
           kind == TokenKind::minus_equal || kind == TokenKind::star_equal ||
           kind == TokenKind::slash_equal || kind == TokenKind::percent_equal ||
           kind == TokenKind::ampersand_equal || kind == TokenKind::pipe_equal ||
           kind == TokenKind::caret_equal || kind == TokenKind::shift_left_equal ||
           kind == TokenKind::shift_right_equal;
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
    if (tokens_.empty() || tokens_.back().kind != TokenKind::end_of_file) {
        tokens_.push_back({TokenKind::end_of_file, {}, {}});
    }
}

Module Parser::parse() {
    Module module;
    while (!at_end()) {
        const std::size_t before = current_;
        try {
            std::vector<Attribute> attributes;
            parse_attributes(attributes);
            if (!check(TokenKind::identifier) || current().lexeme != "fn") {
                report("expected a function declaration (only functions are supported)",
                       current().span.start);
                synchronize();
            } else {
                module.functions.push_back(parse_function(std::move(attributes)));
            }
        } catch (const ParseFailure&) {
            synchronize();
        }
        if (current_ == before && !at_end()) {
            ++current_;
        }
    }
    return module;
}

const std::vector<Diagnostic>& Parser::diagnostics() const noexcept {
    return diagnostics_;
}

const Token& Parser::current() const noexcept {
    return tokens_[current_ < tokens_.size() ? current_ : tokens_.size() - 1];
}

const Token& Parser::previous() const noexcept {
    return tokens_[current_ == 0 ? 0 : current_ - 1];
}

bool Parser::at_end() const noexcept {
    return check(TokenKind::end_of_file);
}

bool Parser::check(TokenKind kind) const noexcept {
    return current().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    if (!at_end()) {
        ++current_;
    }
    return true;
}

const Token& Parser::consume(TokenKind kind, const char* message) {
    if (check(kind)) {
        const Token& token = current();
        if (!at_end()) {
            ++current_;
        }
        return token;
    }
    report(message, current().span.start);
    throw ParseFailure{};
}

void Parser::synchronize() {
    while (!at_end()) {
        if (previous().kind == TokenKind::semicolon) {
            return;
        }
        if (check(TokenKind::at) || (check(TokenKind::identifier) && current().lexeme == "fn")) {
            return;
        }
        ++current_;
    }
}

void Parser::parse_attributes(std::vector<Attribute>& attributes) {
    while (match(TokenKind::at)) {
        const SourceLocation start = previous().span.start;
        const Token& name = consume(TokenKind::identifier, "expected attribute name after '@'");
        Attribute attribute;
        attribute.name = name.lexeme;
        if (match(TokenKind::left_paren)) {
            attribute.arguments = parse_attribute_arguments();
            const Token& close = consume(TokenKind::right_paren,
                                         "expected ')' after attribute arguments");
            attribute.span = {start, close.span.end};
        } else {
            attribute.span = {start, name.span.end};
        }
        attributes.push_back(std::move(attribute));
    }
}

std::vector<std::string> Parser::parse_attribute_arguments() {
    std::vector<std::string> arguments;
    if (check(TokenKind::right_paren)) {
        return arguments;
    }
    std::string argument;
    int nesting = 0;
    while (!at_end()) {
        if (nesting == 0 && check(TokenKind::right_paren)) {
            break;
        }
        if (nesting == 0 && check(TokenKind::comma)) {
            ++current_;
            arguments.push_back(std::move(argument));
            argument.clear();
            continue;
        }
        if (check(TokenKind::left_paren)) ++nesting;
        if (check(TokenKind::right_paren)) --nesting;
        if (!argument.empty()) argument.push_back(' ');
        argument += current().lexeme;
        ++current_;
    }
    if (!argument.empty()) {
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

Function Parser::parse_function(std::vector<Attribute> attributes) {
    const Token& start = consume(TokenKind::identifier, "expected 'fn'");
    const Token& name = consume(TokenKind::identifier, "expected function name");
    Function function;
    function.name = name.lexeme;
    function.attributes = std::move(attributes);
    function.span.start = start.span.start;
    consume(TokenKind::left_paren, "expected '(' after function name");
    if (!check(TokenKind::right_paren)) {
        do {
            function.parameters.push_back(parse_parameter());
        } while (match(TokenKind::comma) && !check(TokenKind::right_paren));
    }
    consume(TokenKind::right_paren, "expected ')' after function parameters");
    if (match(TokenKind::arrow)) {
        std::vector<Attribute> return_attributes;
        parse_attributes(return_attributes);
        function.return_type = parse_type();
    }
    function.body = parse_block();
    function.span.end = previous().span.end;
    return function;
}

Parameter Parser::parse_parameter() {
    Parameter parameter;
    parse_attributes(parameter.attributes);
    const Token& name = consume(TokenKind::identifier, "expected parameter name");
    parameter.name = name.lexeme;
    parameter.span.start = name.span.start;
    consume(TokenKind::colon, "expected ':' after parameter name");
    parameter.type = parse_type();
    parameter.span.end = previous().span.end;
    return parameter;
}

std::string Parser::parse_type() {
    const Token& first = consume(TokenKind::identifier, "expected type");
    std::string type = first.lexeme;
    if (match(TokenKind::less)) {
        type += '<';
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (check(TokenKind::less)) {
                ++depth;
            } else if (check(TokenKind::greater)) {
                --depth;
            } else if (check(TokenKind::shift_right)) {
                if (depth < 2) {
                    report("unexpected '>>' in generic type", current().span.start);
                    throw ParseFailure{};
                }
                depth -= 2;
            }
            if (current().kind == TokenKind::comma) {
                type += ", ";
            } else {
                type += current().lexeme;
            }
            ++current_;
        }
        if (depth != 0) {
            report("expected '>' to close generic type", current().span.start);
            throw ParseFailure{};
        }
    }
    return type;
}

std::unique_ptr<Statement> Parser::parse_block() {
    const Token& open = consume(TokenKind::left_brace, "expected '{' to begin function body");
    auto block = std::make_unique<Statement>();
    block->kind = Statement::Kind::block;
    block->span.start = open.span.start;
    while (!check(TokenKind::right_brace) && !at_end()) {
        const std::size_t before = current_;
        try {
            block->statements.push_back(parse_statement());
        } catch (const ParseFailure&) {
            synchronize();
        }
        if (before == current_ && !at_end()) ++current_;
    }
    const Token& close = consume(TokenKind::right_brace, "expected '}' after function body");
    block->span.end = close.span.end;
    return block;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (check(TokenKind::left_brace)) {
        return parse_block();
    }

    auto statement = std::make_unique<Statement>();
    statement->span.start = current().span.start;
    if (check(TokenKind::identifier) &&
        (current().lexeme == "let" || current().lexeme == "var")) {
        statement->mutable_variable = current().lexeme == "var";
        ++current_;
        statement->kind = Statement::Kind::variable;
        const Token& name = consume(TokenKind::identifier, "expected variable name");
        statement->name = name.lexeme;
        if (match(TokenKind::colon)) statement->type = parse_type();
        if (match(TokenKind::equal)) {
            statement->expression = parse_expression();
        } else if (!statement->type.empty() && statement->mutable_variable) {
            // WGSL permits an explicitly typed var without an initializer.
        } else {
            report("expected ':' or '=' after variable name", current().span.start);
            throw ParseFailure{};
        }
        if (statement->type.empty() && !statement->expression) {
            report("a variable requires a type or initializer", name.span.start);
            throw ParseFailure{};
        }
        consume(TokenKind::semicolon, "expected ';' after variable declaration");
    } else if (check(TokenKind::identifier) && current().lexeme == "return") {
        ++current_;
        statement->kind = Statement::Kind::return_statement;
        if (!check(TokenKind::semicolon)) statement->expression = parse_expression();
        consume(TokenKind::semicolon, "expected ';' after return");
    } else if (check(TokenKind::identifier) && current().lexeme == "if") {
        ++current_;
        statement->kind = Statement::Kind::if_statement;
        statement->expression = parse_expression();
        statement->then_branch = parse_block();
        if (check(TokenKind::identifier) && current().lexeme == "else") {
            ++current_;
            if (check(TokenKind::identifier) && current().lexeme == "if") {
                statement->else_branch = parse_statement();
            } else {
                statement->else_branch = parse_block();
            }
        }
    } else {
        auto lhs = parse_expression();
        if (is_assignment(current().kind)) {
            statement->kind = Statement::Kind::assignment;
            statement->operation = current().lexeme;
            ++current_;
            statement->expression = std::move(lhs);
            auto rhs = parse_expression();
            statement->expression->children.push_back(std::move(rhs));
        } else {
            statement->kind = Statement::Kind::expression;
            statement->expression = std::move(lhs);
        }
        consume(TokenKind::semicolon, "expected ';' after statement");
    }
    statement->span.end = previous().span.end;
    return statement;
}

std::unique_ptr<Expression> Parser::parse_expression() {
    return parse_precedence(1);
}

std::unique_ptr<Expression> Parser::parse_precedence(int minimum_precedence) {
    auto left = parse_prefix();
    for (;;) {
        if (check(TokenKind::left_paren) && 9 >= minimum_precedence) {
            ++current_;
            auto call = std::make_unique<Expression>();
            call->kind = Expression::Kind::call;
            call->span.start = left->span.start;
            call->children.push_back(std::move(left));
            if (!check(TokenKind::right_paren)) {
                do {
                    call->children.push_back(parse_expression());
                } while (match(TokenKind::comma) && !check(TokenKind::right_paren));
            }
            const Token& close = consume(TokenKind::right_paren, "expected ')' after arguments");
            call->span.end = close.span.end;
            left = std::move(call);
            continue;
        }
        if (match(TokenKind::dot)) {
            const Token& member = consume(TokenKind::identifier, "expected member name after '.'");
            auto node = std::make_unique<Expression>();
            node->kind = Expression::Kind::member;
            node->text = member.lexeme;
            node->span = {left->span.start, member.span.end};
            node->children.push_back(std::move(left));
            left = std::move(node);
            continue;
        }

        const int current_precedence = precedence(current().kind);
        if (current_precedence < minimum_precedence) break;
        const Token operation = current();
        ++current_;
        auto right = parse_precedence(current_precedence + 1);
        auto binary = std::make_unique<Expression>();
        binary->kind = Expression::Kind::binary;
        binary->text = operation.lexeme;
        binary->span = {left->span.start, right->span.end};
        binary->children.push_back(std::move(left));
        binary->children.push_back(std::move(right));
        left = std::move(binary);
    }
    return left;
}

std::unique_ptr<Expression> Parser::parse_prefix() {
    const Token token = current();
    if (match(TokenKind::bang) || match(TokenKind::minus) || match(TokenKind::plus) ||
        match(TokenKind::tilde)) {
        auto expression = std::make_unique<Expression>();
        expression->kind = Expression::Kind::unary;
        expression->text = token.lexeme;
        expression->span.start = token.span.start;
        expression->children.push_back(parse_precedence(8));
        expression->span.end = expression->children.front()->span.end;
        return expression;
    }
    if (match(TokenKind::left_paren)) {
        auto expression = parse_expression();
        const Token& close = consume(TokenKind::right_paren, "expected ')' after expression");
        expression->span = {token.span.start, close.span.end};
        return expression;
    }
    if (match(TokenKind::integer_literal) || match(TokenKind::float_literal)) {
        auto expression = std::make_unique<Expression>();
        expression->kind = Expression::Kind::number;
        expression->text = token.lexeme;
        expression->span = token.span;
        return expression;
    }
    if (match(TokenKind::identifier)) {
        auto expression = std::make_unique<Expression>();
        expression->kind = token.lexeme == "true" || token.lexeme == "false"
            ? Expression::Kind::boolean : Expression::Kind::identifier;
        expression->text = token.lexeme;
        expression->span = token.span;
        return expression;
    }
    report("expected expression", current().span.start);
    throw ParseFailure{};
}

int Parser::precedence(TokenKind kind) const noexcept {
    switch (kind) {
    case TokenKind::or_or: return 1;
    case TokenKind::and_and: return 2;
    case TokenKind::pipe: return 3;
    case TokenKind::caret: return 4;
    case TokenKind::ampersand: return 5;
    case TokenKind::equal_equal:
    case TokenKind::bang_equal:
    case TokenKind::less:
    case TokenKind::less_equal:
    case TokenKind::greater:
    case TokenKind::greater_equal: return 6;
    case TokenKind::shift_left:
    case TokenKind::shift_right: return 7;
    case TokenKind::plus:
    case TokenKind::minus: return 8;
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::percent: return 9;
    default: return 0;
    }
}

void Parser::report(std::string message, SourceLocation location) {
    diagnostics_.push_back({DiagnosticSeverity::error, std::move(message), location});
}

} // namespace wgslcheck