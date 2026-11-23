#include "script/Parser.h"

#include "app/AppPaths.h"
#include "core/HumanizationProfile.h"
#include "script/BuiltinFunctions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace undek::script {
namespace {

bool isStatementStart(TokenType t) {
    switch (t) {
        case TokenType::Profile:
        case TokenType::Var:
        case TokenType::Loop:
        case TokenType::If:
        case TokenType::Click:
        case TokenType::PressKey:
        case TokenType::KeyDown:
        case TokenType::KeyUp:
        case TokenType::MouseDown:
        case TokenType::MouseUp:
        case TokenType::MoveTo:
        case TokenType::Scroll:
        case TokenType::Wait:
        case TokenType::While:
        case TokenType::RepeatUntil:
        case TokenType::Break:
        case TokenType::Log:
        case TokenType::RandomWait:
        case TokenType::WaitUntil:
        case TokenType::Include:
        case TokenType::Call:
            return true;
        default:
            return false;
    }
}

bool isMouseButtonName(const std::string& name) {
    std::string lower = name;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "left" || lower == "right" || lower == "middle" || lower == "x1" ||
           lower == "x2";
}

} // namespace

Parser::Parser(std::vector<Token> tokens, int include_depth, int source_id) :
    tokens_(std::move(tokens)), include_depth_(include_depth), source_id_(source_id) {}

const Token& Parser::peek() const {
    static const Token end_token{TokenType::End, "", 0, 0, 0};
    if (tokens_.empty() || pos_ >= tokens_.size()) return end_token;
    return tokens_[pos_];
}

const Token& Parser::advance() {
    if (tokens_.empty()) return peek();
    if (pos_ >= tokens_.size() - 1) return tokens_.back();
    return tokens_[pos_++];
}

bool Parser::check(TokenType t) const {
    return peek().type == t;
}

bool Parser::match(TokenType t) {
    if (!check(t)) return false;
    advance();
    return true;
}

std::string Parser::lineErr(const std::string& msg) const {
    return "line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column) +
           ": " + msg;
}

void Parser::expect(TokenType t, const char* msg) {
    if (!match(t)) throw std::runtime_error(lineErr(msg));
}

double Parser::expectNumber(const char* msg) {
    if (!check(TokenType::Number) && !check(TokenType::Ms)) {
        throw std::runtime_error(lineErr(msg));
    }
    const double value = advance().number;
    // 1e999 is a valid token but not a number anything can use.
    if (!std::isfinite(value)) {
        throw std::runtime_error(lineErr(std::string(msg) + ": the number is too large"));
    }
    return value;
}

double Parser::expectSignedNumber(const char* msg) {
    const bool negative = match(TokenType::Minus);
    const double value = expectNumber(msg);
    return negative ? -value : value;
}

void Parser::synchronize() {
    while (!check(TokenType::End)) {
        if (check(TokenType::RBrace) || check(TokenType::Error) || isStatementStart(peek().type))
            return;
        advance();
    }
}

Program Parser::parse() {
    Program prog;
    while (!check(TokenType::End)) {
        try {
            prog.statements.push_back(statement());
        } catch (const std::exception& e) {
            errors_.push_back(e.what());
            synchronize();
            while (check(TokenType::RBrace)) {
                errors_.push_back(lineErr("unexpected '}'"));
                advance();
                synchronize();
            }
        }
    }
    if (!errors_.empty()) {
        std::string msg;
        for (size_t i = 0; i < errors_.size(); ++i) {
            if (i) msg += "\n";
            msg += errors_[i];
        }
        throw std::runtime_error(msg);
    }
    for (const auto& s : prog.statements) {
        if (s->kind == StmtKind::Profile) {
            prog.script_sigma = static_cast<ProfileStmt*>(s.get())->default_sigma;
        }
    }
    return prog;
}

std::unique_ptr<Stmt> Parser::statement() {
    if (check(TokenType::Error)) {
        const Token t = advance();
        throw std::runtime_error("line " + std::to_string(t.line) + ", column " +
                                 std::to_string(t.column) + ": " + t.text);
    }

    const int line = peek().line;
    std::unique_ptr<Stmt> s;
    if (check(TokenType::Profile))
        s = profileStatement();
    else if (check(TokenType::Var))
        s = varStatement();
    else if (check(TokenType::Loop))
        s = loopStatement();
    else if (check(TokenType::If))
        s = ifStatement();
    else if (check(TokenType::Click))
        s = clickStatement();
    else if (check(TokenType::PressKey))
        s = pressKeyStatement();
    else if (check(TokenType::KeyDown))
        s = keyDownStatement();
    else if (check(TokenType::KeyUp))
        s = keyUpStatement();
    else if (check(TokenType::MouseDown))
        s = mouseDownStatement();
    else if (check(TokenType::MouseUp))
        s = mouseUpStatement();
    else if (check(TokenType::MoveTo))
        s = moveToStatement();
    else if (check(TokenType::Scroll))
        s = scrollStatement();
    else if (check(TokenType::Wait))
        s = waitStatement();
    else if (check(TokenType::While))
        s = whileStatement();
    else if (check(TokenType::RepeatUntil))
        s = repeatUntilStatement();
    else if (check(TokenType::Break)) {
        advance();
        s = std::make_unique<BreakStmt>();
    } else if (check(TokenType::Log))
        s = logStatement();
    else if (check(TokenType::RandomWait))
        s = randomWaitStatement();
    else if (check(TokenType::WaitUntil))
        s = waitUntilStatement();
    else if (check(TokenType::Include))
        s = includeStatement();
    else if (check(TokenType::Call))
        s = callStatement();
    else
        throw std::runtime_error(lineErr("Unexpected token '" + peek().text + "'"));

    s->line = line;
    s->source_id = source_id_;
    return s;
}

std::string Parser::parseKeyArg() {
    if (check(TokenType::String)) return advance().text;
    if (check(TokenType::Number)) {
        const double value = expectNumber("Expected key number");
        if (value < 0.0 || value > 9.0) {
            throw std::runtime_error(lineErr("a key written as a number must be 0-9"));
        }
        return std::to_string(static_cast<int>(std::lround(value)));
    }
    if (check(TokenType::Identifier)) return advance().text;
    throw std::runtime_error(lineErr("Expected key name"));
}

std::string Parser::parseOptionalButtonArg() {
    if (check(TokenType::RParen)) return "right";
    if (check(TokenType::String) || check(TokenType::Identifier)) {
        const Token t = advance();
        if (!isMouseButtonName(t.text)) {
            throw std::runtime_error("line " + std::to_string(t.line) + ", column " +
                                     std::to_string(t.column) + ": unknown mouse button '" +
                                     t.text + "'");
        }
        return t.text;
    }
    return "right";
}

std::unique_ptr<Stmt> Parser::profileStatement() {
    auto stmt = std::make_unique<ProfileStmt>();
    expect(TokenType::Profile, "Expected profile");
    if (!check(TokenType::Identifier)) {
        throw std::runtime_error(lineErr("profile needs a setting, e.g. profile default_sigma=12"));
    }
    const std::string key = advance().text;
    if (key != "default_sigma") {
        // Silently ignoring this made a typo look like it had worked.
        throw std::runtime_error(lineErr("unknown profile setting '" + key + "'"));
    }
    expect(TokenType::Equals, "Expected =");
    stmt->default_sigma = expectNumber("Expected sigma value");
    return stmt;
}

SearchArea Parser::parseRegionArg() {
    const char* shape = "region expects Rect(x, y, width, height)";
    if (!check(TokenType::Identifier) || peek().text != "Rect") {
        throw std::runtime_error(lineErr(shape));
    }
    advance();
    expect(TokenType::LParen, "Expected ( after Rect");

    const auto coord = [this](const char* what) {
        const double value = expectNumber(what);
        if (std::fabs(value) > 1000000.0) {
            throw std::runtime_error(lineErr(std::string(what) + " is out of range"));
        }
        return static_cast<int>(std::lround(value));
    };

    SearchArea area;
    area.x = coord("Expected region x");
    expect(TokenType::Comma, "Expected ,");
    area.y = coord("Expected region y");
    expect(TokenType::Comma, "Expected ,");
    area.width = coord("Expected region width");
    expect(TokenType::Comma, "Expected ,");
    area.height = coord("Expected region height");
    if (area.width <= 0 || area.height <= 0) {
        throw std::runtime_error(lineErr("region needs a width and height above 0"));
    }
    expect(TokenType::RParen, "Expected )");
    return area;
}

Vec2 Parser::parsePointExpr(std::unique_ptr<PointExpr>& out) {
    expect(TokenType::Point, "Expected Point");
    expect(TokenType::LParen, "Expected (");
    auto x = expression();
    expect(TokenType::Comma, "Expected ,");
    auto y = expression();
    expect(TokenType::RParen, "Expected )");

    // Plain numbers stay as a plain Vec2 so nothing changes for existing scripts.
    if (x->kind == Expr::Kind::Number && y->kind == Expr::Kind::Number) {
        return {x->number, y->number};
    }
    out = std::make_unique<PointExpr>();
    out->x = std::move(x);
    out->y = std::move(y);
    return {};
}

std::unique_ptr<Expr> Parser::expression() {
    return additive();
}

std::unique_ptr<Expr> Parser::numberOrExpr(double& literal) {
    auto value = expression();
    if (value->kind == Expr::Kind::Number) {
        literal = value->number;
        return nullptr;
    }
    return value;
}

std::unique_ptr<Expr> Parser::additive() {
    auto left = multiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        const TokenType op = advance().type;
        auto right = multiplicative();
        auto node = std::make_unique<Expr>();
        node->kind = (op == TokenType::Plus) ? Expr::Kind::Add : Expr::Kind::Subtract;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::multiplicative() {
    auto left = unaryExpr();
    while (check(TokenType::Star) || check(TokenType::Slash)) {
        const TokenType op = advance().type;
        auto right = unaryExpr();
        auto node = std::make_unique<Expr>();
        node->kind = (op == TokenType::Star) ? Expr::Kind::Multiply : Expr::Kind::Divide;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::unaryExpr() {
    if (match(TokenType::Minus)) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Negate;
        node->lhs = unaryExpr();
        return node;
    }
    match(TokenType::Plus);
    return primary();
}

std::unique_ptr<Expr> Parser::primary() {
    if (check(TokenType::Number) || check(TokenType::Ms)) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Number;
        node->number = advance().number;
        return node;
    }
    if (check(TokenType::Identifier)) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Variable;
        node->name = advance().text;
        return node;
    }
    if (match(TokenType::LParen)) {
        auto inner = expression();
        expect(TokenType::RParen, "Expected )");
        return inner;
    }
    throw std::runtime_error(lineErr("Expected a number or variable"));
}

std::unique_ptr<Stmt> Parser::varStatement() {
    auto stmt = std::make_unique<VarAssignStmt>();
    expect(TokenType::Var, "Expected var");
    if (!check(TokenType::Identifier)) throw std::runtime_error(lineErr("Expected variable name"));
    stmt->name = advance().text;
    expect(TokenType::Equals, "Expected =");

    if (check(TokenType::Point)) {
        stmt->point = parsePointExpr(stmt->point_expr);
    } else if (match(TokenType::FindImage)) {
        stmt->is_image = true;
        expect(TokenType::LParen, "Expected (");
        if (!check(TokenType::String)) {
            throw std::runtime_error(lineErr("FindImage expects a file path string"));
        }
        stmt->image_path = advance().text;
        while (match(TokenType::Comma)) {
            if (!check(TokenType::Identifier)) {
                throw std::runtime_error(lineErr("Expected named argument"));
            }
            const std::string key = advance().text;
            expect(TokenType::Equals, "Expected =");
            if (key == "tolerance") {
                const double tol = expectNumber("tolerance expects a number");
                if (tol < 0.0 || tol > 255.0) {
                    throw std::runtime_error(lineErr("tolerance must be between 0 and 255"));
                }
                stmt->image_tolerance = static_cast<int>(tol);
            } else if (key == "region") {
                stmt->image_region = parseRegionArg();
            } else {
                throw std::runtime_error(lineErr("unknown FindImage argument '" + key + "'"));
            }
        }
        expect(TokenType::RParen, "Expected )");
    } else {
        stmt->is_number = true;
        stmt->value = expression();
    }
    return stmt;
}

void Parser::parseNamedArgs(bool& humanize, double& radius, double& box_w, double& box_h,
                            double& hold_min, double& hold_max) {
    bool saw_hold_min = false;
    bool saw_hold_max = false;
    while (check(TokenType::Identifier)) {
        const std::string key = advance().text;
        expect(TokenType::Equals, "Expected =");
        if (key == "humanize") {
            if (check(TokenType::True)) {
                humanize = true;
                advance();
            } else if (check(TokenType::False)) {
                humanize = false;
                advance();
            } else
                throw std::runtime_error(lineErr("humanize expects true or false"));
        } else if (key == "radius") {
            radius = expectSignedNumber("Expected radius value");
            if (radius < 0.0) throw std::runtime_error(lineErr("radius can't be negative"));
        } else if (key == "box_w" || key == "half_w") {
            box_w = expectSignedNumber("Expected box width");
            if (box_w < 0.0) throw std::runtime_error(lineErr("box_w can't be negative"));
        } else if (key == "box_h" || key == "half_h") {
            box_h = expectSignedNumber("Expected box height");
            if (box_h < 0.0) throw std::runtime_error(lineErr("box_h can't be negative"));
        } else if (key == "hold_min") {
            hold_min = expectSignedNumber("Expected hold_min value");
            if (hold_min < 0.0) throw std::runtime_error(lineErr("hold_min can't be negative"));
            if (hold_min > kMaxHoldMs)
                throw std::runtime_error(lineErr("hold_min is past the 600000 ms limit"));
            saw_hold_min = true;
        } else if (key == "hold_max") {
            hold_max = expectSignedNumber("Expected hold_max value");
            if (hold_max < 0.0) throw std::runtime_error(lineErr("hold_max can't be negative"));
            if (hold_max > kMaxHoldMs)
                throw std::runtime_error(lineErr("hold_max is past the 600000 ms limit"));
            saw_hold_max = true;
        } else {
            // Silently ignoring a typo'd argument made scripts look like they
            // worked while the value was never used.
            throw std::runtime_error(lineErr("unknown argument '" + key + "'"));
        }
        if (!check(TokenType::Comma)) break;
        advance();
    }
    if (saw_hold_min && saw_hold_max && hold_max < hold_min) {
        throw std::runtime_error(lineErr("hold_max can't be smaller than hold_min"));
    }
}

std::unique_ptr<Stmt> Parser::clickStatement() {
    auto stmt = std::make_unique<ClickStmt>();
    expect(TokenType::Click, "Expected Click");
    expect(TokenType::LParen, "Expected (");

    if (check(TokenType::Point)) {
        stmt->point = parsePointExpr(stmt->point_expr);
    } else if (check(TokenType::Identifier)) {
        stmt->use_var = true;
        stmt->target_var = advance().text;
    } else {
        throw std::runtime_error(lineErr("Click expects Point(...) or variable name"));
    }

    if (check(TokenType::Comma)) {
        advance();
        double dummy_min = 0, dummy_max = 0;
        parseNamedArgs(stmt->humanize, stmt->radius, stmt->box_half_w, stmt->box_half_h, dummy_min,
                       dummy_max);
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::pressKeyStatement() {
    auto stmt = std::make_unique<PressKeyStmt>();
    expect(TokenType::PressKey, "Expected PressKey");
    expect(TokenType::LParen, "Expected (");
    if (!check(TokenType::String)) throw std::runtime_error(lineErr("Expected key string"));
    stmt->key = advance().text;
    if (check(TokenType::Comma)) {
        advance();
        double r = 0, bw = 0, bh = 0;
        parseNamedArgs(stmt->humanize, r, bw, bh, stmt->hold_min, stmt->hold_max);
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::keyDownStatement() {
    auto stmt = std::make_unique<KeyDownStmt>();
    expect(TokenType::KeyDown, "Expected KeyDown");
    expect(TokenType::LParen, "Expected (");
    stmt->key = parseKeyArg();
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::keyUpStatement() {
    auto stmt = std::make_unique<KeyUpStmt>();
    expect(TokenType::KeyUp, "Expected KeyUp");
    expect(TokenType::LParen, "Expected (");
    stmt->key = parseKeyArg();
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::mouseDownStatement() {
    auto stmt = std::make_unique<MouseDownStmt>();
    expect(TokenType::MouseDown, "Expected MouseDown");
    expect(TokenType::LParen, "Expected (");
    stmt->button = parseOptionalButtonArg();
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::mouseUpStatement() {
    auto stmt = std::make_unique<MouseUpStmt>();
    expect(TokenType::MouseUp, "Expected MouseUp");
    expect(TokenType::LParen, "Expected (");
    stmt->button = parseOptionalButtonArg();
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::moveToStatement() {
    auto stmt = std::make_unique<MoveToStmt>();
    expect(TokenType::MoveTo, "Expected MoveTo");
    expect(TokenType::LParen, "Expected (");
    if (check(TokenType::Point)) {
        stmt->point = parsePointExpr(stmt->point_expr);
    } else if (check(TokenType::Identifier)) {
        stmt->use_var = true;
        stmt->target_var = advance().text;
    } else {
        throw std::runtime_error(lineErr("MoveTo expects Point(...) or variable name"));
    }
    if (check(TokenType::Comma)) {
        advance();
        double r = 0, bw = 0, bh = 0, hmin = 0, hmax = 0;
        parseNamedArgs(stmt->humanize, r, bw, bh, hmin, hmax);
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::scrollStatement() {
    auto stmt = std::make_unique<ScrollStmt>();
    expect(TokenType::Scroll, "Expected Scroll");
    expect(TokenType::LParen, "Expected (");
    double amount = 0.0;
    stmt->delta_expr = numberOrExpr(amount);
    if (!stmt->delta_expr && std::fabs(amount) > 1000000.0) {
        throw std::runtime_error(lineErr("Scroll amount is out of range"));
    }
    stmt->delta = static_cast<int>(std::lround(amount));
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::waitStatement() {
    auto stmt = std::make_unique<WaitStmt>();
    expect(TokenType::Wait, "Expected Wait");
    expect(TokenType::LParen, "Expected (");
    stmt->mean_expr = numberOrExpr(stmt->mean_ms);
    // A literal skips the check in expectNumber, and a huge number is still a
    // valid double.
    if (!stmt->mean_expr && stmt->mean_ms > kMaxWaitMs) {
        throw std::runtime_error(lineErr("Wait is past the 86400000 ms limit"));
    }
    if (check(TokenType::Identifier) && peek().text == "ms") {
        advance();
    }
    if (check(TokenType::Comma)) {
        advance();
        double r = 0, bw = 0, bh = 0, hmin = 0, hmax = 0;
        parseNamedArgs(stmt->humanize, r, bw, bh, hmin, hmax);
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::vector<std::unique_ptr<Stmt>> Parser::block() {
    std::vector<std::unique_ptr<Stmt>> body;
    expect(TokenType::LBrace, "Expected {");
    while (!check(TokenType::RBrace) && !check(TokenType::End)) {
        try {
            body.push_back(statement());
        } catch (const std::exception& e) {
            errors_.push_back(e.what());
            synchronize();
        }
    }
    expect(TokenType::RBrace, "Expected }");
    return body;
}

std::unique_ptr<Stmt> Parser::loopStatement() {
    auto stmt = std::make_unique<LoopStmt>();
    expect(TokenType::Loop, "Expected loop");
    double count = 0.0;
    stmt->count_expr = numberOrExpr(count);
    // A literal past the int range used to narrow to a negative count, which
    // read as "loop zero times" instead of letting the cap decide. This is the
    // same range the VM clamps an expression count to.
    stmt->count = static_cast<int>(std::lround(std::clamp(count, -kMaxLoopCount, kMaxLoopCount)));
    stmt->body = block();
    return stmt;
}

std::unique_ptr<Stmt> Parser::ifStatement() {
    auto stmt = std::make_unique<IfStmt>();
    expect(TokenType::If, "Expected if");
    stmt->cond = condition();
    stmt->then_branch = block();
    if (match(TokenType::Else)) {
        stmt->else_branch = block();
    }
    return stmt;
}

Condition Parser::condition() {
    Condition c;
    c.negate = match(TokenType::Not);
    if (match(TokenType::WindowTitle)) {
        c.type = Condition::Type::WindowTitle;
        expect(TokenType::LParen, "Expected (");
        if (!check(TokenType::String))
            throw std::runtime_error(lineErr("WindowTitle expects a string"));
        c.text = advance().text;
        expect(TokenType::RParen, "Expected )");
        return c;
    }
    if (match(TokenType::ImageExists)) {
        c.type = Condition::Type::Image;
        expect(TokenType::LParen, "Expected (");
        if (!check(TokenType::String)) {
            throw std::runtime_error(lineErr("ImageExists expects a file path string"));
        }
        c.image_path = advance().text;
        while (match(TokenType::Comma)) {
            if (!check(TokenType::Identifier)) {
                throw std::runtime_error(lineErr("Expected named argument"));
            }
            const std::string key = advance().text;
            expect(TokenType::Equals, "Expected =");
            if (key == "tolerance") {
                const double tol = expectNumber("tolerance expects a number");
                if (tol < 0.0 || tol > 255.0) {
                    throw std::runtime_error(lineErr("tolerance must be between 0 and 255"));
                }
                c.tolerance = static_cast<int>(tol);
            } else if (key == "region") {
                c.region = parseRegionArg();
            } else {
                throw std::runtime_error(lineErr("unknown ImageExists argument '" + key + "'"));
            }
        }
        expect(TokenType::RParen, "Expected )");
        return c;
    }
    if (!match(TokenType::PixelAt)) {
        throw std::runtime_error(lineErr("Expected PixelAt, WindowTitle or ImageExists condition"));
    }
    expect(TokenType::LParen, "Expected (");
    const auto pixel = [this](const char* what) {
        const double value = expectNumber(what);
        if (std::fabs(value) > 1000000.0) {
            throw std::runtime_error(lineErr(std::string(what) + " is out of range"));
        }
        return static_cast<int>(std::lround(value));
    };
    c.pixel_x = pixel("Expected pixel x");
    expect(TokenType::Comma, "Expected ,");
    c.pixel_y = pixel("Expected pixel y");
    expect(TokenType::RParen, "Expected )");

    if (!check(TokenType::Equals)) {
        throw std::runtime_error(
            lineErr("PixelAt needs a colour comparison, e.g. PixelAt(x, y) == 0xFF0000"));
    }
    advance();
    expect(TokenType::Equals, "Expected == after PixelAt(...)");
    c.expected_color = static_cast<unsigned int>(expectNumber("Expected a colour after =="));
    c.has_color_check = true;

    if (check(TokenType::Identifier) && peek().text == "tolerance") {
        advance();
        if (!check(TokenType::Number)) {
            throw std::runtime_error(lineErr("tolerance expects a number between 0 and 255"));
        }
        const double tol = advance().number;
        if (tol < 0.0 || tol > 255.0) {
            throw std::runtime_error(lineErr("tolerance must be between 0 and 255"));
        }
        c.tolerance = static_cast<int>(tol);
    }
    return c;
}

std::unique_ptr<Stmt> Parser::whileStatement() {
    auto stmt = std::make_unique<WhileStmt>();
    expect(TokenType::While, "Expected while");
    stmt->cond = condition();
    stmt->body = block();
    return stmt;
}

std::unique_ptr<Stmt> Parser::repeatUntilStatement() {
    auto stmt = std::make_unique<RepeatUntilStmt>();
    expect(TokenType::RepeatUntil, "Expected repeat_until");
    stmt->cond = condition();
    stmt->body = block();
    return stmt;
}

std::unique_ptr<Stmt> Parser::logStatement() {
    auto stmt = std::make_unique<LogStmt>();
    expect(TokenType::Log, "Expected Log");
    expect(TokenType::LParen, "Expected (");
    if (!check(TokenType::String)) throw std::runtime_error(lineErr("Log expects a string"));
    stmt->message = advance().text;
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::randomWaitStatement() {
    auto stmt = std::make_unique<RandomWaitStmt>();
    expect(TokenType::RandomWait, "Expected RandomWait");
    expect(TokenType::LParen, "Expected (");
    stmt->min_ms = expectNumber("Expected min ms");
    expect(TokenType::Comma, "Expected ,");
    stmt->max_ms = expectNumber("Expected max ms");
    if (stmt->min_ms > stmt->max_ms) std::swap(stmt->min_ms, stmt->max_ms);
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::waitUntilStatement() {
    auto stmt = std::make_unique<WaitUntilStmt>();
    expect(TokenType::WaitUntil, "Expected WaitUntil");
    expect(TokenType::LParen, "Expected (");
    stmt->cond = condition();
    while (match(TokenType::Comma)) {
        if (!check(TokenType::Identifier)) {
            throw std::runtime_error(lineErr("Expected named argument"));
        }
        const std::string key = advance().text;
        expect(TokenType::Equals, "Expected =");
        if (key == "timeout") {
            stmt->timeout_ms = expectSignedNumber("Expected timeout ms");
            if (stmt->timeout_ms < 0.0) {
                throw std::runtime_error(lineErr("timeout can't be negative"));
            }
        } else {
            throw std::runtime_error(lineErr("unknown argument '" + key + "'"));
        }
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::callStatement() {
    auto stmt = std::make_unique<CallStmt>();
    expect(TokenType::Call, "Expected Call");
    expect(TokenType::LParen, "Expected (");
    if (!check(TokenType::String)) {
        throw std::runtime_error(lineErr("Call expects the registered function name as a string"));
    }
    stmt->name = advance().text;
    while (match(TokenType::Comma)) {
        CallArg arg;
        if (check(TokenType::String)) {
            arg.is_string = true;
            arg.text = advance().text;
        } else {
            arg.number = expression();
        }
        stmt->args.push_back(std::move(arg));
    }
    expect(TokenType::RParen, "Expected )");
    return stmt;
}

std::unique_ptr<Stmt> Parser::includeStatement() {
    constexpr int kMaxIncludeDepth = 8;
    auto stmt = std::make_unique<IncludeStmt>();
    expect(TokenType::Include, "Expected include");
    if (!check(TokenType::String))
        throw std::runtime_error(lineErr("include expects a file path string"));
    stmt->path = advance().text;
    if (include_depth_ >= kMaxIncludeDepth) {
        throw std::runtime_error("include nesting too deep (possible cycle): " + stmt->path);
    }
    std::string source = readFile(stmt->path);
    if (source.empty()) source = readFile(AppPaths::resolve(stmt->path));
    if (source.empty()) throw std::runtime_error("Include not found: " + stmt->path);
    Lexer lexer(source);
    Parser sub(lexer.tokenize(), include_depth_ + 1, source_id_ + 1);
    stmt->body = std::move(sub.parse().statements);
    return stmt;
}

} // namespace undek::script
