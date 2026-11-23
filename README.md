# Undek Macro

Undek Macro is a Windows desktop macro tool and `.undek` script runner written in C++17. It pairs a clean ImGui editor with natural input timing and curved mouse movement so macros don't feel robotic. It's also lightweight as the whole program is about 1.22 MB with no runtime dependencies.

## Installing

Download the latest release from [Releases](https://github.com/Tw1kslv/Undek-Macro/releases/latest). There are two ways, and they're the same program.

**Installer (recommended).** Run `UndekMacro-<version>-setup.exe`. It installs for the current user only into `%LOCALAPPDATA%\Programs\Undek Macro`, and offers a desktop shortcut, an "Add to Start menu" option and "start with Windows" as optional checkboxes. Your macros, profiles and bindings live in that folder. Uninstalling removes the program and leaves your macros alone.

**Portable zip.** Download `UndekMacro-Windows-x64.zip`, extract it anywhere and run `undek.exe`. The zip includes `profiles/`, `bindings/`, `examples/` and `macros/`, so there's nothing else to install.

Either way, the first start creates whatever is missing (folders, a default profile, a starter macro) and shows a short welcome panel once. Nothing that already exists gets overwritten. See [CHANGELOG.md](CHANGELOG.md) for what changed between versions.

### Optional: Interception driver backend

For lower level, silent, driver based input injection (can be more undetected):

1. Download and install the [Interception driver](https://github.com/oblitum/Interception) run `install-interception.exe` as Administrator and reboot.
2. Put `interception.dll` (from the Interception build, it isn't bundled here) next to `undek.exe`. It is loaded from the app's folder (or system32) and nowhere else, so a copy sitting in some other folder on `PATH` is ignored rather than executed.
3. Set `input_backend` to `interception` (or leave it on `auto`) in the macros settings, GUI's *Input backend & packet delay* section shows which backend is active.

If the DLL or driver isn't found, Undek automatically falls back to `SendInput`, so this step is entirely optional.

## Features

- **Desktop Editor & Runner:** Create, edit, dry-run, and manage `.undek` scripts in one window, complete with syntax highlighting, line numbers, a live execution log, and an interactive path preview where you pick the start and target points, bend the curve, wobble and overshoot, and see the curve handles plus which parts of the move take the longest. The bar above the execution log can be dragged to give the script more room (double-click it to go back to the default height), Duplicate / Rename / Delete live on right-click in the macro list, and the **Help** menu opens this README (or Notepad, if Windows has nothing registered for `.md`), the grammar in `docs/language.md`, the macros and examples folders, and a list of the current shortcuts.

- **Macro Debugger:** Set breakpoints by clicking the line-number gutter, then step through a script statement by statement. The Debugger window shows the paused line, script variables, currently held keys/buttons, and an execution history.

- **Interactive Bind Detection (Keyboard & Mouse Buttons):** Click **Detect** and press any key combo or mouse button (`Mouse4`, `Mouse5`, `Mouse3`, etc.) to bind it directly to a macro. Side buttons go through a little mouse hook rather than a once-per-frame check, so a quick tap still counts and macros that click `x1`/`x2` can't set themselves off.

- **Side-button hotkeys:** A macro bound to Mouse4/Mouse5/Mouse3 works even on a fast double tap (the second press arrives as a different Windows message), and every press is passed on unchanged, because the Interception driver's events reach a Windows hook looking exactly like hardware, so eating one would also eat the macro's own `MouseDown("x1")`/`MouseDown("x2")`.

- **Global Hotkeys (`Ctrl + Shift + Q` Toggle):** Bind macros to hotkeys that work in the background. Pressing `Ctrl + Shift + Q` toggles all macros on and off (with optional sound cues and a volume slider) and aborts any running script while releasing held keys. Releasing wins over everything else: mouse-up and key-up skip the inter-packet throttle and the cancel check, and each held key/button is released on its own even if one of them fails, so a Stop can't leave something pressed down. Only one macro runs at a time across the editor, hotkeys and `listen`, so a second one is refused ("Another macro is already running") instead of interleaving input, and starting the editor twice brings the running window forward rather than opening a second copy that would fight it for the cursor. `Ctrl + Shift + P` pauses a running macro at its next step and carries on when you press it again - the pause can be set before the run starts, one pressed just as Run is hit still lands, and Stop still ends a paused run instead of waiting for a resume. A long `Wait` is broken into short hops so the pause takes effect within a few dozen milliseconds. A key that's being held or a cursor partway along a humanized path finishes that action first, so nothing is left pressed or half-moved. Profiles, bindings and settings are written through a temp file and swapped in one step, so a crash mid-save can't leave you with a truncated or missing config. Loading is all-or-nothing too: a profile with a bad value is rejected and the one in use stays as it was, and reloading bindings replaces them instead of appending a second copy.

- **Tray & Stealth Hide Hotkeys:** Hide to the system tray with `Ctrl + Shift + H`, or use the Stealth Hide hotkey (`Ctrl + Shift + X`) to hide the window, taskbar icon, and tray icon completely-pressing it again restores whichever state you had before.

- **Live Coordinate & Color Picker (`Ctrl + Shift + C`):** Press `Ctrl + Shift + C` anywhere to grab the cursor `(x, y)` and hex pixel color `0xRRGGBB` under your mouse, automatically appending a snippet into the editor and copying it to your clipboard.

- **Global Input Recorder:** Click **Record** to hook live keyboard and mouse actions (with real wait times) and insert them into your script. Keys the recorder has no friendly name for are stored as raw scancodes (for example `KeyDown("scan:0x1e")`), so a take doesn't quietly lose a press, and those play back through both input backends. Cursor movement, the mouse wheel and side buttons are captured too, **Pause** lets you skip a section while keeping one take, and Undek ignores its own injected input so it can't record itself. A small **RECORDER ON** strip stays on screen while a take runs. Hitting Stop Rec opens **Recorded actions**, where you can retime any step, untick ones you don't want, reorder or delete them, and left drags are shown as a single group. Releases are never lost: let go of a key while paused and the matching `KeyUp` is written when you resume, stopping the take closes out anything still pressed, and Insert refuses a take that would leave a key or button held down (so a recording can't play back stuck).

- **Visual Macro Builder:** A window in **View → Visual builder** where you pick an action (wait, move, click, press/hold a key, hold/release a mouse button, scroll), fill in the coordinates or name, and add it to a list. **Insert into script** appends the generated `.undek` code, so people who don't want to type code can still build a macro, and the result is ordinary script you can edit by hand afterwards. Tick steps and wrap them in a `loop`, an `if WindowTitle(...)` or an `if PixelAt(...)` to repeat or guard part of the sequence, and press **Add here** on a block to put the next steps inside it. Ticks work at any depth, so a selection inside a block gets wrapped inside that block.

- **Per-Window Auto-Profile Switching:** Automatically switch humanization profiles depending on which window title is currently focused.

- **Humanized Timing & Mouse Movement:**
  - *Presets:* The Timing section has four one-click presets - Safe, Balanced, Aggressive and Off. They fill in the humanization sliders below and leave your safety limits, required window and input backend untouched, so picking one mid-session won't loosen a cap. Save the profile if you want to keep the result.
  - *Delay diagnostics:* **Sample this profile** in the Timing section times a typical 40 ms wait 400 times and prints min / p10 / median / p90 / max, so you can see what a profile actually does before trusting it on a real macro. It only reads the timers - nothing is sent to the game.
  - *Gaussian Delays:* Waits use bell-curve timing rather than fixed sleeps.
  - *Fatigue & Focus Cycles:* Shifts base speeds by 5–15% and widens timing spread every 2–3 minutes.
  - *Micro Stalls:* Occasional 180–350ms hesitation pauses between actions.
  - *Key Hold & Finger Overlap:* Hold times scale independently from transit waits, with occasional longer holds and slight finger overlap (`MouseDown` firing just before `KeyUp` finishes). The `hold_min`/`hold_max` you give are the base range - hold scaling and the occasional panic stretch on purpose take a hold outside it, by roughly 15% short and up to the configured panic multiplier long. `humanize=false` sticks to the range exactly.
  - *Rarely Missed Inputs:* Small configurable chance to skip a complete press+release pair without ever leaving a key held down.
  - *Curved Mouse Paths:* Moves along smooth Bézier curves with Fitts-style speed scaling, subtle wobble, and occasional target overshoot.
  - *Input Backends:* Supports Win32 `SendInput`, optional hardware scancode injection via `interception.dll`, and a minimum inter-packet delay (15-20ms default). Packet spacing applies to discrete key/button events only - cursor movement keeps its own path timing so mouse paths stay smooth.

## Building

Requirements: Windows 10/11, Visual Studio (MSVC with C++17), and CMake 3.20+. The code is formatted with `clang-format` 23.1.5 (see `.clang-format`), and [docs/architecture.md](docs/architecture.md) explains how a script travels from text to injected input if you want to work on it.

```powershell
cmake -B build -DUNDEK_BUILD_TESTS=OFF
cmake --build build --config Release
```

Configure needs the single-header `nlohmann/json` 3.11.3. CMake fetches it into the build folder and checks it against a pinned SHA256. If you're building offline, drop the header at `third_party/nlohmann/json.hpp` or configure with `-DUNDEK_JSON_DIR=<folder that contains nlohmann/json.hpp>` and CMake won't touch the network.

This builds `build\Release\undek.exe`. Run it with no arguments for the GUI, or use the CLI:

```powershell
.\build\Release\undek.exe
.\build\Release\undek.exe run examples\showcase_all_features.undek
.\build\Release\undek.exe dry-run examples\combo_humanized.undek
.\build\Release\undek.exe check examples\showcase_all_features.undek
.\build\Release\undek.exe listen --bindings bindings\default.json
.\build\Release\undek.exe --version
```

`run` and `dry-run` exit with `0` when the script finishes and `1` when the file, the profile or the script itself is bad, so they're safe to call from a batch file. `check` reads a script without running it and exits `1` if it finds something: a parse error, a `Call` to a function nothing registered or one whose arguments don't fit what it declared, or a picture that's missing or isn't a BMP it can read. Pictures are looked for in the same places a run looks: the path as written, then beside the script, then in the app folder. It needs no screen, so it works in CI. `--version` and `--help` print and exit `0`.

In `listen` mode the macro hotkeys still respond while a script is running, and the stop hotkey cancels the running macro first time you press it, press it again when nothing is running to turn the macros off. Only one macro runs at a time there too, so a hotkey pressed while the editor is mid-run just logs that something is already running.

### Testing

```powershell
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`test_script` covers the language, VM, debugger and the image conditions, the visual builder (everything it writes is handed to the real parser, so the builder and the language can't drift apart), every file in `examples/` (parsed and dry-run, so a bundled example can't rot), `test_vision` the BMP reader and the template search, `test_robustness` feeds junk to the lexer, parser and BMP loader (fixed seed, so a failure is reproducible), `test_functions` the contract a C++ function has to keep to be callable from a script (what `add()` accepts, how a call is measured against the declaration, and lookups while other threads register and remove), `test_gaussian` the timing distributions (seeded reproducibility, clamp bounds, drift/noise limits, and that miss/overlap rates land on their configured probabilities) and `test_bezier` the path generator. None of them pull in the Windows GUI or the input layer, so they build and run on Linux as well - which is handy, because that's where I can run them before touching anything real. CI runs the same suite a second time under AddressSanitizer and UBSan (`tests-sanitizers`), which is how memory mistakes in the image and parsing code get caught rather than shipped, and once more with gcovr (`coverage`) so a change that adds a lot of code and no tests shows up as a drop in the report.

The Windows job doesn't stop at building the zip: it runs the packaged `undek.exe`, asks for `--version`, and runs `undek check` over every example in the package, so a release that builds but can't start is caught before it's uploaded.

The lexer, the parser and the BMP reader also have libFuzzer targets in `fuzz/`, for clang builds:

```powershell
cmake -B build-fuzz -DUNDEK_BUILD_FUZZERS=ON -DUNDEK_BUILD_TESTS=OFF -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz --config Release
.\build-fuzz\Release\fuzz_script.exe
.\build-fuzz\Release\fuzz_bmp.exe
```

Both take an optional folder to keep a corpus in, without one they start from nothing and find their own way in. The BMP target calls the in-memory decoder, so it doesn't write a file for every case.

The `Fuzz` workflow runs both on a nightly timer, seeds them from `examples/`, and keeps the corpus from run to run, so an input that took an hour to find doesn't have to be found again. A crash stops the job and the input is attached to it. That file goes into `tests/` as a fixed case rather than being left for the fuzzer to rediscover.

## If Something Is Wrong

- **Nothing happens in the game.** Some games ignore `SendInput` on purpose. Try the Interception backend (above) but if it still doesn't work, the game is probably filtering driver input too and there's not much a user mode tool can do about it.
- **Input goes to the wrong window.** Windows only delivers input to the focused window. Use `require_window` in the profile, or `if WindowTitle("...")` in the script, so a run doesn't spray keys into whatever got focus.
- **Clicks work but the game ignores them.** Several games ignore injected clicks while their window is not the foreground one, and some only accept clicks that arrive with a matching cursor position. Keep `humanize=true` and don't move the mouse yourself during a run.
- **An antivirus flags `undek.exe`.** Global key hooks plus input injection is exactly what a keylogger looks like, and unsigned builds get flagged by heuristics. A local build from source is usually trusted more, and adding an exclusion is the other option.
- **Hotkeys do nothing in an elevated app.** Windows blocks a normal-integrity process from injecting into an admin window. Run both elevated or neither.
- **`ImageExists` never matches.** The template has to be an uncompressed 24/32-bit BMP and captured at the same scaling the game uses. HDR, fullscreen-exclusive modes and DPI scaling can change the pixels. A little `tolerance` covers fading and compression, a lot of it starts matching the wrong thing.
- **Colours in the picker are off.** HDR displays and Windows colour management can shift what you see from what `GetPixel` returns. Disable HDR while picking colours.

## Default Hotkeys

| Hotkey | Action |
|--------|--------|
| `Ctrl + Shift + Q` | Toggle all macros ON / OFF (also stops any running macro) |
| `Ctrl + Shift + H` | Show / hide window to the system tray |
| `Ctrl + Shift + X` | Stealth hide (hides window, taskbar, and tray icon, press again to restore previous state) |
| `Ctrl + Shift + C` | Capture cursor `(x, y)` and pixel color `0xRRGGBB` into the editor |
| `F5` / `F6` | Run / Dry-run the open script |
| `F7` | Start a debugger session (pauses before the first statement) |
| `F9` | Toggle breakpoint at the caret line |
| `F10` / `F8` | Step / Continue while the debugger is paused |
| `Ctrl + S` / `Ctrl + N` | Save script / New macro |
| `F1` | Open the scripting reference |

All hotkeys can be changed in **Settings** or **Hotkey Bindings** using the **Detect** button, and **Help → Keyboard shortcuts** always shows what they are set to right now.

## All Functions (`.undek` Script Reference)

The complete grammar, every argument and the error behaviour live in [docs/language.md](docs/language.md).

Strings understand `\"` and `\\`, which is what the visual builder writes, every other backslash is kept as typed, so `"C:\temp\ui.bmp"` still works. Named arguments are checked: a typo like `radious=4` is a parse error instead of being silently ignored, and negative sizes, negative holds and a `hold_max` smaller than `hold_min` are refused before anything runs.

### Timing & Profiles

- **`profile default_sigma=<ms>`** - Sets the default timing spread (in ms) for humanized waits in the script. It's the only setting `profile` takes, so a typo is reported instead of skipped.
- **`Wait(<time>, humanize=true|false)`** - Pauses for `<time>` (`100ms`, `0.02s`, etc., or an expression like `d * 2`). Uses hold scaling while a key/button is held, or transit timing (with Fatigue & Focus cycles and Micro Stalls) between actions.
- **`RandomWait(<min_ms>, <max_ms>)`** - Waits a random duration between `min_ms` and `max_ms`.
- **Safety limits** - A profile caps `max_loop_iterations`, `max_total_instructions` and `max_runtime_sec`. The runtime cap now cuts long waits and mouse paths short instead of only being checked between statements, so a runaway script stops quickly. A single wait is capped at 86400000 ms (a day) so a huge duration is reported instead of hanging the run.

### Keyboard & Mouse Input

- **`PressKey("<key>", hold_min=<ms>, hold_max=<ms>, humanize=true|false)`** - Presses and releases a key (`"e"`, `"space"`, `"enter"`, `"tab"`, `"esc"`, `"shift"`, `"ctrl"`, `"alt"`, `"backspace"`, `"delete"`, `"insert"`, `"home"`, `"end"`, `"pageup"`, `"pagedown"`, the arrow keys, `"capslock"`, `"numpad0"`–`"numpad9"`, `"f1"`–`"f12"`, or any single character - symbols requiring Shift like `"!"` or `"A"` are typed correctly, and characters without a key on your layout, such as `"é"` or `"ж"`, are typed through Unicode input). Key names can also be raw scancodes like `"scan:0x1e"` (or `"scan_e:0x4b"` for the E0-prefixed ones), which is what the recorder writes when it doesn't know a name. Unknown key names are reported in the Execution Log instead of being skipped silently, and a key or click Windows refuses to inject (a locked screen, an elevated window, `SendInput` returning short) shows up as `[input] ... failed` and stops the macro, so it can't carry on as though the input had landed. Set `"input_failure_policy": "continue"` in a profile to log those failures and keep going instead.
- **`Wait(<time>, humanize=false)`** - With `humanize=false`, waits the exact requested time with no randomization, drift, or minimum-delay clamp.
- **`KeyDown("<key>")` / `KeyUp("<key>")`** - Holds or releases a key. Consecutive `KeyUp` and `MouseDown`/`KeyDown` calls can slightly overlap when finger overlap is enabled.
- **`MouseDown("<button>")` / `MouseUp("<button>")`** - Holds or releases `"left"`, `"right"` (default), `"middle"`, or side buttons `"x1"`/`"x2"`. A button name it doesn't know is reported as a script error instead of quietly turning into a right click.
- **`MoveTo(<Point|var>, humanize=true|false)`** - Moves the cursor without clicking. `humanize=true` follows a curved path. The default goes straight there, which is what the recorder writes. Absolute moves use the whole virtual desktop, so monitors placed left of or above the main one still land where you expect.
- **`Scroll(<amount>)`** - Turns the mouse wheel. `-120` is one notch down, `120` is one notch up, an expression is fine too.
- **`var <name> = Point(<x>, <y>)`** - Stores screen coordinates in a variable.
- **`var <name> = <number>`** - Stores a number (a `ms` suffix is allowed). Arithmetic with `+ - * /`, parentheses and unary minus works inside `Point(...)`, so `var spot = Point(x + 20, y - 10);` then `MoveTo(spot)` does what it looks like. Unknown names and division by zero are script errors.
- **`Click(<Point|var>, humanize=true|false, radius=<px>, box_w=<px>, box_h=<px>)`** - Moves the cursor to the target and left-clicks. With `humanize=true`, picks a random offset inside `radius` or `box_w`/`box_h` and follows a curved path. With `humanize=false` it lands exactly on the point - no curve, no aiming spread and none of the profile's occasional cursor noise - which is what you want when a macro has to hit one specific pixel.

### Control Flow, Screen Checks & Helpers

- **`if [not] <condition> {
    ... } else {
    ... }`** - Conditional branch.
- **`PixelAt(<x>, <y>) == 0xRRGGBB [tolerance <0-255>]`** - Checks if screen pixel `(x, y)` matches the hex RGB color within `tolerance`. The `==` and the colour are required, `tolerance` has to stay between 0 and 255, and a pixel the app can't read never counts as a match.
- **`WindowTitle("<text>")`** - Checks if the active window title contains `<text>` (case-insensitive).
- **`ImageExists("<file.bmp>", tolerance=<0-255>)`** - Checks if a picture is somewhere on screen. A relative path is looked for as written, then beside the script, then in the app folder, which is also how `check` looks for it. Use it in `if`/`while`/`repeat_until`/`WaitUntil` like the other conditions, e.g. `if ImageExists("shop_open.bmp") {
    Click(Point(400, 300)) }` or `WaitUntil(ImageExists("loaded.bmp"), timeout=8000)`. The template has to be an uncompressed 24- or 32-bit `.bmp` (crop one with Paint, or any screenshot tool set to BMP); relative paths are looked up next to the exe. Without `tolerance` every pixel has to match exactly, which is brittle when a game scales or fades anything, so something like `tolerance=12` is usually kinder. The log says which of the four things happened - found, not on screen, `invalid image` (the file is there but isn't a BMP it can read) or a capture failure - because those need different fixes. A header claiming more than 32768 px per side or 64M pixels is refused as a mistake rather than a template. The log line says where it was found and how close the match was, the file is loaded once per run, and the capture covers the whole virtual desktop, so coordinates are screen coordinates. `region=Rect(x, y, width, height)` keeps the search inside that rectangle, in desktop coordinates, which is noticeably faster on a big screen and is how you tell two identical icons in different windows apart. Full-screen grabs are not free - checking half a dozen templates in a tight `while` loop will cost you frames, though the capture is reused between checks and a yes/no check stops at the first match, so it's far cheaper than it was, and a big scan stops early when you hit Stop or the runtime limit. Coordinates come back as desktop coordinates, so a monitor placed left of or above the primary one doesn't shift where the click lands.
- **`Call("<Name>", <args>...)`** - Runs a function that was registered from C++ with `FunctionRegistry`. Numbers and expressions (`Call("Scale", n * 2)`) and strings (`Call("Say", "hello")`) can be mixed, the result goes to the log, and a function that returns an error logs `[call] Name failed: ...` without stopping the script. Calling a name that was never registered is an error, which is deliberate - a typo should be visible. A registered function carries a description, its argument list, a return type and a version, so a future function browser or the docs can be generated from the registry instead of hand-written, and it gets a small context with cancellation, elapsed/remaining time and logging. This is still a foundation rather than a plugin folder: there is no DLL loading, so a function only exists if the build registers it (the tests show the shape).
- **`var p = FindImage("<file.bmp>", tolerance=<0-255>, region=Rect(x, y, width, height))`** - Same search, but stores the **centre** of the picture in a point variable, so `Click(p)` lands in the middle of the button instead of its top-left corner. If the picture isn't there the run stops with `FindImage: not on screen (...)` rather than clicking something random - guard it with `if ImageExists(...)` when you expect it to be missing sometimes.
- **`loop <count> {
    ... }`** - Repeats a block `<count>` times. The count can be an expression.
- **`while [not] <condition> {
    ... }`** - Repeats while the condition is true.
- **`repeat_until [not] <condition> {
    ... }`** - Runs the block at least once and repeats until the condition is true.
- **`WaitUntil(<condition>, timeout=<ms>)`** - Polls a condition every ~30ms until it's true or `timeout` (default 5000ms) expires, then continues. Works with `not`, `PixelAt`, `WindowTitle`, and `ImageExists`.
- **`break`** - Exits the current loop early.
- **`Log("<message>")`** - Prints a message to the Execution Log and `logs/` file.
- **`include "<path.undek>"`** - Inlines another `.undek` script.

## Example Script

```text
profile default_sigma=12

// Hold "4", release, and right-click
KeyDown("4")
Wait(0.015s, humanize=true)
KeyUp("4")
Wait(0.02s, humanize=true)

MouseDown("right")
Wait(0.025s, humanize=true)
MouseUp("right")
```

More are in `examples/`.
