# How Undek Macro is put together

This is the "where does my change go" page. It follows one script from text to
injected input, then covers the parts that don't fit on that line.

## The path of a script

```text
string -> Lexer -> tokens -> Parser -> AST -> Vm -> ActionScheduler -> IInputBackend
```

- **Lexer** (`src/script/Lexer.*`) — turns source into tokens with line and
  column. Keywords, numbers, strings and punctuation only; it knows nothing
  about meaning.
- **Parser** (`src/script/Parser.*`) — builds the AST (`src/script/Ast.h`) and
  is where the whole argument/type checking lives. It parses the entire file
  before anything runs and collects as many errors as it can.
- **Vm** (`src/script/Vm.*`) — walks the AST, owns variables, the instruction
  and runtime budgets, and decides what each statement means. It talks to input
  through `IInputBackend` and, for humanized work, through `ActionScheduler`.
- **ActionScheduler** (`src/core/ActionScheduler.*`) — the timing layer: sleeps,
  path playback, click jitter, key holds. It's the only place that knows a
  wait has to respect both `max_runtime_sec` and Stop.
- **IInputBackend** (`src/input/IInputBackend.h`) — the OS boundary. `SendInput`
  and the Interception driver are two implementations; `ThrottledInputBackend`
  wraps either one to space packets out. `InputResult` carries failures back so
  the VM can stop the macro on one (or log it, when the profile asks to carry
  on) and release anything a refused press may have left held.

Adding a new statement therefore means: token in `Lexer.h`/`Lexer.cpp`, node in
`Ast.h`, parse function in `Parser`, a `case` in `Vm::execute`, a description in
`DebugController::describeStmt` (the compiler warns if you forget), and a line in
`docs/language.md`.

## Cancellation, and why it's centralised

Everything that can block checks one flag: `cancel::` (`src/core/Cancel.h`).
`runs::tryBegin()` (`src/core/RunSlot.*`) hands out a single run slot, so the
editor, the hotkeys and `listen` can't run two macros at once, and it clears the
previous run's stop request while holding the same lock Stop takes. Stop is
therefore never lost, and a fresh run never inherits it.

Pause is part of a run for the same reason. `runs::requestPause()` takes that
lock before it sets the flag and `runs::end()` takes it while it clears it, so a
pause that arrives as the run finishes is either cleared with it or refused,
and the next run starts unpaused.

The rule that follows: **releases never wait**. `keyUp`/`mouseUp` skip both the
packet throttle and the cancel check, because a stop that interrupts a release
leaves a key physically down. `Vm::releaseHeldInputs()` then releases each held
input once, however many lists it is on, and tries each one separately so one
failure can't skip the rest.

## Threads

- **GUI thread** — ImGui rendering and all editor state. Never blocks on input.
- **Run thread** (`src/app/ScriptRunner.*`) — parses and executes one script,
  logs through a callback, and is the only writer of the run slot's owner.
- **Hook thread** (`src/input/InputRecorder.*`) — the low-level keyboard and
  mouse hooks. It owns the recorder's state under one mutex.
- **Hotkey host** (`src/bindings/HotkeyHost.*`) — message loop for `listen`.

Ownership is either a `unique_ptr` (AST nodes, backend wrappers) or a plain
reference for things that outlive the caller (VM holding a backend). The VM's
`FunctionRegistry` is the one global mutable piece. Everything registers before
a run starts; `remove()` exists for teardown, and a call already in flight keeps
the copy it took.

## Vision

`ScreenGrab` captures the whole virtual desktop with GDI into an `ImageFrame`
that remembers its origin, so coordinates come back as desktop coordinates.
`vision/Image.*` is pure logic — BMP loading, tolerant template matching — and
knows nothing about screens or scripts, which is why it's fully testable
without Windows. The VM caches templates per run and passes a checkpoint into
the matcher so Stop and the runtime limit can break a big scan.

## Randomization

`RandomizationEngine` owns one seeded `mt19937` and all the probability work;
`PathGenerator` turns distances into Bézier steps using that engine;
`HumanizationSession` holds the fatigue cycle so several engines can share it.
Profiles are plain structs with `validate()`, and every stochastic decision goes
through the engine rather than the standard library directly — which is what
makes the statistical tests in `tests/test_gaussian.cpp` possible.

## Testing seams

- `IInputBackend` — tests use recording/failing backends instead of injecting.
- `Vm::setScreenGrabber` — tests feed a fake screen instead of a desktop.
- `Vm::setFunctionRegistry` — tests register their own `Call` functions.
- `FunctionRegistry` — `tests/test_functions.cpp` pins the registration contract
  (metadata checks, signature matching, lookups while entries change), since
  that's the only seam a C++ extension can use.
- Everything in `src/vision`, `src/core` and `src/script` builds without the
  GUI, which is what lets CI run the same tests on Linux.

## What isn't here

The GUI (`src/editor/UndekApp.*`) is deliberately thin: it edits text and
profile fields and starts runs. There is no plugin DLL loading, and the only
extension point is registering a C++ function in `FunctionRegistry` before a
run starts.
