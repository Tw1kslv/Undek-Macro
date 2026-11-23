#pragma once

#include "script/Ast.h"
#include "script/Lexer.h"

#include <string>
#include <vector>

namespace undek::script {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, int include_depth = 0, int source_id = 0);
    Program parse();

private:
    const Token& peek() const;
    const Token& advance();
    bool match(TokenType t);
    bool check(TokenType t) const;
    void expect(TokenType t, const char* msg);
    double expectNumber(const char* msg);
    double expectSignedNumber(const char* msg);
    std::string lineErr(const std::string& msg) const;

    std::unique_ptr<Stmt> statement();
    std::unique_ptr<Stmt> profileStatement();
    std::unique_ptr<Stmt> varStatement();
    std::unique_ptr<Stmt> loopStatement();
    std::unique_ptr<Stmt> ifStatement();
    std::unique_ptr<Stmt> clickStatement();
    std::unique_ptr<Stmt> pressKeyStatement();
    std::unique_ptr<Stmt> keyDownStatement();
    std::unique_ptr<Stmt> keyUpStatement();
    std::unique_ptr<Stmt> mouseDownStatement();
    std::unique_ptr<Stmt> mouseUpStatement();
    std::unique_ptr<Stmt> moveToStatement();
    std::unique_ptr<Stmt> scrollStatement();
    std::unique_ptr<Stmt> waitStatement();
    std::unique_ptr<Stmt> whileStatement();
    std::unique_ptr<Stmt> repeatUntilStatement();
    std::unique_ptr<Stmt> logStatement();
    std::unique_ptr<Stmt> randomWaitStatement();
    std::unique_ptr<Stmt> waitUntilStatement();
    std::unique_ptr<Stmt> includeStatement();
    std::unique_ptr<Stmt> callStatement();
    Condition condition();
    std::vector<std::unique_ptr<Stmt>> block();
    void synchronize();

    SearchArea parseRegionArg();
    std::string parseKeyArg();
    std::string parseOptionalButtonArg();
    Vec2 parsePointExpr(std::unique_ptr<PointExpr>& out);
    std::unique_ptr<Expr> expression();
    // Plain literals stay literal, so existing scripts get the same AST.
    std::unique_ptr<Expr> numberOrExpr(double& literal);
    std::unique_ptr<Expr> additive();
    std::unique_ptr<Expr> multiplicative();
    std::unique_ptr<Expr> unaryExpr();
    std::unique_ptr<Expr> primary();
    void parseNamedArgs(bool& humanize, double& radius, double& box_w, double& box_h,
                        double& hold_min, double& hold_max);

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    int include_depth_ = 0;
    int source_id_ = 0;
    std::vector<std::string> errors_;
};

} // namespace undek::script
