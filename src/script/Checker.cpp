#include "script/Checker.h"

#include "script/Ast.h"
#include "script/FunctionRegistry.h"
#include "script/Lexer.h"
#include "script/Parser.h"
#include "script/PicturePath.h"
#include "vision/Image.h"

#include <stdexcept>
#include <utility>

namespace undek::script {
namespace {

int lineFromMessage(const std::string& message) {
    // Parse errors read "line 12, column 4: ...".
    const std::string tag = "line ";
    const auto at = message.find(tag);
    if (at != std::string::npos) {
        const std::string rest = message.substr(at + tag.size());
        try {
            return std::stoi(rest);
        } catch (const std::exception&) {
            return 0;
        }
    }
    return 0;
}

// Numbers aren't evaluated here - checking has no variables to read - but a
// number expression can only ever produce a number, which is all the signature
// check needs to know.
std::vector<ScriptValue> callArguments(const CallStmt& call) {
    std::vector<ScriptValue> args;
    args.reserve(call.args.size());
    for (const auto& arg : call.args) {
        args.push_back(arg.is_string ? ScriptValue::str(arg.text) : ScriptValue::num(0.0));
    }
    return args;
}

class Walker {
public:
    Walker(std::string base_dir, std::vector<ScriptProblem>& problems) :
        base_dir_(std::move(base_dir)), problems_(problems) {}

    void statements(const std::vector<std::unique_ptr<Stmt>>& list) {
        for (const auto& stmt : list) {
            if (stmt) statement(*stmt);
        }
    }

private:
    void add(const Stmt& stmt, std::string message) {
        problems_.push_back({stmt.line, std::move(message)});
    }

    void picture(const Stmt& stmt, const std::string& path) {
        if (path.empty()) return;
        const std::string found = resolvePicturePath(path, base_dir_);
        if (found.empty()) {
            add(stmt, "picture not found: " + path);
            return;
        }
        ImageFrame frame;
        std::string error;
        // Reading it now is how a run would find out, except a run finds out when
        // it reaches the line and the user has already lost that time.
        if (!loadBmp(found, frame, error)) {
            add(stmt, "picture can't be read: " + path + " (" + error + ")");
        }
    }

    void condition(const Stmt& stmt, const Condition& cond) {
        if (cond.type == Condition::Type::Image) picture(stmt, cond.image_path);
    }

    void statement(const Stmt& stmt) {
        switch (stmt.kind) {
            case StmtKind::Call: {
                const auto& call = static_cast<const CallStmt&>(stmt);
                FunctionInfo declared;
                if (!FunctionRegistry::global().findInfo(call.name, declared)) {
                    add(stmt, "no script function named '" + call.name + "'");
                    break;
                }
                // The same check a run makes before it hands the function its
                // arguments, so a call that would fail at that point is caught
                // here instead, where nothing has been typed anywhere yet.
                std::string mismatch;
                if (!FunctionRegistry::checkSignature(declared, callArguments(call), mismatch)) {
                    add(stmt, mismatch);
                }
                break;
            }
            case StmtKind::VarAssign: {
                const auto& assign = static_cast<const VarAssignStmt&>(stmt);
                if (assign.is_image) picture(stmt, assign.image_path);
                break;
            }
            case StmtKind::IfStmt: {
                const auto& branch = static_cast<const IfStmt&>(stmt);
                condition(stmt, branch.cond);
                statements(branch.then_branch);
                statements(branch.else_branch);
                break;
            }
            case StmtKind::While: {
                const auto& loop = static_cast<const WhileStmt&>(stmt);
                condition(stmt, loop.cond);
                statements(loop.body);
                break;
            }
            case StmtKind::RepeatUntil: {
                const auto& loop = static_cast<const RepeatUntilStmt&>(stmt);
                condition(stmt, loop.cond);
                statements(loop.body);
                break;
            }
            case StmtKind::WaitUntil: {
                const auto& wait = static_cast<const WaitUntilStmt&>(stmt);
                condition(stmt, wait.cond);
                break;
            }
            case StmtKind::Loop: {
                statements(static_cast<const LoopStmt&>(stmt).body);
                break;
            }
            case StmtKind::Include: {
                statements(static_cast<const IncludeStmt&>(stmt).body);
                break;
            }
            default:
                break;
        }
    }

    std::string base_dir_;
    std::vector<ScriptProblem>& problems_;
};

} // namespace

std::vector<ScriptProblem> checkScript(const std::string& source, const std::string& base_dir) {
    std::vector<ScriptProblem> problems;
    if (source.empty()) {
        problems.push_back({0, "the script is empty"});
        return problems;
    }

    Program program;
    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize(), 0, 0, base_dir);
        program = parser.parse();
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        problems.push_back({lineFromMessage(message), message});
        return problems;
    }

    Walker walker(base_dir, problems);
    walker.statements(program.statements);
    return problems;
}

} // namespace undek::script
