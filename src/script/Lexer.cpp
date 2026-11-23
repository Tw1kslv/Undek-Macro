#include "script/Lexer.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace undek::script {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

char Lexer::peek() const {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::advance() {
    if (pos_ >= source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char c) {
    if (peek() != c) return false;
    advance();
    return true;
}

void Lexer::skipWhitespace() {
    while (std::isspace(static_cast<unsigned char>(peek())))
        advance();
}

void Lexer::skipComment() {
    if (peek() == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
        while (peek() && peek() != '\n')
            advance();
    }
}

Token Lexer::number() {
    Token t;
    t.type = TokenType::Number;
    t.line = line_;
    t.column = column_;
    std::string buf;

    if (peek() == '0' && pos_ + 1 < source_.size() &&
        (source_[pos_ + 1] == 'x' || source_[pos_ + 1] == 'X')) {
        buf.push_back(advance());
        buf.push_back(advance());
        size_t digits = 0;
        while (std::isxdigit(static_cast<unsigned char>(peek()))) {
            buf.push_back(advance());
            ++digits;
        }
        t.text = buf;
        if (digits == 0) {
            t.type = TokenType::Error;
            t.text = "malformed hex number '" + buf + "'";
            return t;
        }
        t.number = static_cast<double>(std::strtoul(buf.c_str() + 2, nullptr, 16));
        return t;
    }

    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
        buf.push_back(advance());
    }
    t.text = buf;
    try {
        size_t consumed = 0;
        t.number = std::stod(buf, &consumed);
        if (consumed != buf.size()) {
            t.type = TokenType::Error;
            t.text = "malformed number '" + buf + "'";
            return t;
        }
    } catch (...) {
        t.type = TokenType::Error;
        t.text = "malformed number '" + buf + "'";
        return t;
    }
    if (match('m') && match('s')) {
        t.type = TokenType::Ms;
    } else if (match('s')) {
        t.type = TokenType::Ms;
        t.number *= 1000.0;
        // The seconds suffix is a multiply the parser never gets to see, so a
        // value that overflows here has to be turned away here.
        if (!std::isfinite(t.number)) {
            t.type = TokenType::Error;
            t.text = "number is too large '" + buf + "s'";
            return t;
        }
    }
    return t;
}

Token Lexer::stringLiteral() {
    Token t;
    t.type = TokenType::String;
    t.line = line_;
    t.column = column_;
    advance(); // opening quote
    while (peek() && peek() != '"' && peek() != '\n') {
        const char c = advance();
        // Only the two escapes the macro builder writes are decoded, so a path
        // like "C:\temp\ui.bmp" keeps its backslashes.
        if (c == '\\' && (peek() == '"' || peek() == '\\')) {
            t.text.push_back(advance());
        } else {
            t.text.push_back(c);
        }
    }
    if (peek() != '"') {
        t.type = TokenType::Error;
        t.text = "unterminated string literal";
        return t;
    }
    advance();
    return t;
}

Token Lexer::identifierOrKeyword() {
    static const std::unordered_map<std::string, TokenType> kws = {
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"loop", TokenType::Loop},
        {"var", TokenType::Var},
        {"profile", TokenType::Profile},
        {"Point", TokenType::Point},
        {"Click", TokenType::Click},
        {"PressKey", TokenType::PressKey},
        {"KeyDown", TokenType::KeyDown},
        {"KeyUp", TokenType::KeyUp},
        {"MouseDown", TokenType::MouseDown},
        {"MouseUp", TokenType::MouseUp},
        {"MoveTo", TokenType::MoveTo},
        {"Scroll", TokenType::Scroll},
        {"Wait", TokenType::Wait},
        {"PixelAt", TokenType::PixelAt},
        {"ImageExists", TokenType::ImageExists},
        {"FindImage", TokenType::FindImage},
        {"Call", TokenType::Call},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"while", TokenType::While},
        {"repeat_until", TokenType::RepeatUntil},
        {"break", TokenType::Break},
        {"not", TokenType::Not},
        {"include", TokenType::Include},
        {"WindowTitle", TokenType::WindowTitle},
        {"RandomWait", TokenType::RandomWait},
        {"WaitUntil", TokenType::WaitUntil},
        {"Log", TokenType::Log},
    };

    Token t;
    t.line = line_;
    t.column = column_;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        t.text.push_back(advance());
    }
    auto it = kws.find(t.text);
    t.type = (it != kws.end()) ? it->second : TokenType::Identifier;
    return t;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (pos_ < source_.size()) {
        // Skipping a comment lands on the newline and then the next line, which
        // may be another comment - so this has to loop, not just run once.
        for (;;) {
            skipWhitespace();
            if (pos_ >= source_.size()) break;
            if (peek() != '/' || pos_ + 1 >= source_.size() || source_[pos_ + 1] != '/') break;
            skipComment();
        }
        if (pos_ >= source_.size()) break;

        const int sl = line_;
        const int sc = column_;
        const char c = peek();

        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(number());
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(identifierOrKeyword());
            continue;
        }
        if (c == '"') {
            tokens.push_back(stringLiteral());
            continue;
        }

        Token t;
        t.line = sl;
        t.column = sc;
        advance();
        switch (c) {
            case '{':
                t.type = TokenType::LBrace;
                break;
            case '}':
                t.type = TokenType::RBrace;
                break;
            case '(':
                t.type = TokenType::LParen;
                break;
            case ')':
                t.type = TokenType::RParen;
                break;
            case ',':
                t.type = TokenType::Comma;
                break;
            case '=':
                t.type = TokenType::Equals;
                break;
            case '.':
                t.type = TokenType::Dot;
                break;
            case '+':
                t.type = TokenType::Plus;
                break;
            case '-':
                t.type = TokenType::Minus;
                break;
            case '*':
                t.type = TokenType::Star;
                break;
            case '/':
                t.type = TokenType::Slash;
                break; // "//" is a comment, handled above
            default:
                t.type = TokenType::Error;
                t.text = "unexpected character '" + std::string(1, c) + "'";
                break;
        }
        tokens.push_back(t);
    }

    tokens.push_back({TokenType::End, "", 0, line_, column_});
    return tokens;
}

} // namespace undek::script
