#pragma once

#include "core/Vec2.h"
#include "vision/Image.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace undek::script {

enum class StmtKind {
    Profile,
    VarAssign,
    Loop,
    IfStmt,
    Click,
    PressKey,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MoveTo,
    Scroll,
    Wait,
    While,
    RepeatUntil,
    Break,
    Log,
    RandomWait,
    WaitUntil,
    Include,
    Call,
};

struct Stmt {
    StmtKind kind;
    int line = 0;
    int source_id = 0;
    virtual ~Stmt() = default;
};

struct ProfileStmt : Stmt {
    double default_sigma = 12.0;
    ProfileStmt() { kind = StmtKind::Profile; }
};

// A small arithmetic tree. Kept separate from Stmt so point arguments can use
// it without every statement needing its own copy of the parser.
struct Expr {
    enum class Kind { Number, Variable, Negate, Add, Subtract, Multiply, Divide };
    Kind kind = Kind::Number;
    double number = 0;
    std::string name;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct PointExpr {
    std::unique_ptr<Expr> x;
    std::unique_ptr<Expr> y;
};

struct VarAssignStmt : Stmt {
    std::string name;
    Vec2 point;
    std::unique_ptr<PointExpr> point_expr;
    bool is_number = false;
    bool is_image = false; // var p = FindImage("ui.bmp") -> centre of the match
    std::string image_path;
    int image_tolerance = 0;
    SearchArea image_region;
    std::unique_ptr<Expr> value;
    VarAssignStmt() { kind = StmtKind::VarAssign; }
};

struct ClickStmt : Stmt {
    std::string target_var;
    Vec2 point;
    std::unique_ptr<PointExpr> point_expr;
    bool use_var = false;
    double radius = 0;
    double box_half_w = 0;
    double box_half_h = 0;
    bool humanize = false;
    ClickStmt() { kind = StmtKind::Click; }
};

struct PressKeyStmt : Stmt {
    std::string key;
    double hold_min = 45;
    double hold_max = 85;
    bool humanize = false;
    PressKeyStmt() { kind = StmtKind::PressKey; }
};

struct KeyDownStmt : Stmt {
    std::string key;
    KeyDownStmt() { kind = StmtKind::KeyDown; }
};

struct KeyUpStmt : Stmt {
    std::string key;
    KeyUpStmt() { kind = StmtKind::KeyUp; }
};

struct MouseDownStmt : Stmt {
    std::string button = "right";
    MouseDownStmt() { kind = StmtKind::MouseDown; }
};

struct MouseUpStmt : Stmt {
    std::string button = "right";
    MouseUpStmt() { kind = StmtKind::MouseUp; }
};

struct MoveToStmt : Stmt {
    std::string target_var;
    Vec2 point;
    std::unique_ptr<PointExpr> point_expr;
    bool use_var = false;
    bool humanize = false;
    MoveToStmt() { kind = StmtKind::MoveTo; }
};

struct ScrollStmt : Stmt {
    int delta = 0;
    std::unique_ptr<Expr> delta_expr;
    ScrollStmt() { kind = StmtKind::Scroll; }
};

struct WaitStmt : Stmt {
    double mean_ms = 0;
    std::unique_ptr<Expr> mean_expr;
    bool humanize = false;
    WaitStmt() { kind = StmtKind::Wait; }
};

// A loop count past this is capped by max_loop_iterations anyway. Keeping
// literals and expressions inside the same range means neither can narrow on
// the way into the int the VM reads.
constexpr double kMaxLoopCount = 1000000000.0;

// A wait is a delay, not a way to park the script. Both the parser and the VM
// hold a single wait to this, so a value that would sleep for years is refused
// instead of accepted and then never finishing.
constexpr double kMaxWaitMs = 86400000.0;

struct LoopStmt : Stmt {
    int count = 0;
    std::unique_ptr<Expr> count_expr;
    std::vector<std::unique_ptr<Stmt>> body;
    LoopStmt() { kind = StmtKind::Loop; }
};

struct Condition {
    enum class Type { PixelAt, WindowTitle, Image };
    Type type = Type::PixelAt;
    int pixel_x = 0;
    int pixel_y = 0;
    unsigned int expected_color = 0;
    bool has_color_check = false;
    int tolerance = 0;
    std::string text;
    std::string image_path;
    SearchArea region;
    bool negate = false;
};

struct IfStmt : Stmt {
    Condition cond;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::unique_ptr<Stmt>> else_branch;
    IfStmt() { kind = StmtKind::IfStmt; }
};

struct WhileStmt : Stmt {
    Condition cond;
    std::vector<std::unique_ptr<Stmt>> body;
    WhileStmt() { kind = StmtKind::While; }
};

struct RepeatUntilStmt : Stmt {
    Condition cond;
    std::vector<std::unique_ptr<Stmt>> body;
    RepeatUntilStmt() { kind = StmtKind::RepeatUntil; }
};

struct BreakStmt : Stmt {
    BreakStmt() { kind = StmtKind::Break; }
};

struct LogStmt : Stmt {
    std::string message;
    LogStmt() { kind = StmtKind::Log; }
};

struct RandomWaitStmt : Stmt {
    double min_ms = 0;
    double max_ms = 0;
    RandomWaitStmt() { kind = StmtKind::RandomWait; }
};

struct WaitUntilStmt : Stmt {
    Condition cond;
    double timeout_ms = 5000.0;
    WaitUntilStmt() { kind = StmtKind::WaitUntil; }
};

struct CallArg {
    bool is_string = false;
    std::string text;
    std::unique_ptr<Expr> number;
};

struct CallStmt : Stmt {
    std::string name;
    std::vector<CallArg> args;
    CallStmt() { kind = StmtKind::Call; }
};

struct IncludeStmt : Stmt {
    std::string path;
    std::vector<std::unique_ptr<Stmt>> body;
    IncludeStmt() { kind = StmtKind::Include; }
};

struct Program {
    std::vector<std::unique_ptr<Stmt>> statements;
    double script_sigma = 12.0;
};

} // namespace undek::script
