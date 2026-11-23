#pragma once

#include <string>
#include <vector>

namespace undek::script {

enum class TokenType {
    End,
    Error,
    Identifier,
    Number,
    String,
    LBrace,
    RBrace,
    LParen,
    RParen,
    Comma,
    Equals,
    Dot,
    Plus,
    Minus,
    Star,
    Slash,
    Ms,
    True,
    False,
    If,
    Else,
    Loop,
    Var,
    Profile,
    Point,
    Click,
    PressKey,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MoveTo,
    Scroll,
    Wait,
    PixelAt,
    ImageExists,
    FindImage,
    While,
    RepeatUntil,
    Break,
    Not,
    Include,
    WindowTitle,
    RandomWait,
    WaitUntil,
    Log,
    Call,
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    double number = 0;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> tokenize();

private:
    void skipWhitespace();
    void skipComment();
    Token identifierOrKeyword();
    Token number();
    Token stringLiteral();
    char peek() const;
    char advance();
    bool match(char c);

    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace undek::script
