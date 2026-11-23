# The `.undek` language

This is the reference for what a `.undek` script can contain. If something here
and the code disagree, the code is right and this page is a bug worth fixing.

## Shape of a script

A script is a list of statements. Whitespace and `//` comments are ignored, a
newline means nothing, and braces group statements into blocks. There are no
semicolons.

```text
// hold 4, then right click
KeyDown("4")
Wait(15ms, humanize=true)
KeyUp("4")
Wait(20ms, humanize=true)
MouseDown("right")
Wait(25ms)
MouseUp("right")
```

Filenames end in `.undek` by convention. `include "other.undek"` pastes another
file in place, up to eight levels deep.

## Values

There are two kinds of value, and they don't mix implicitly:

- **Numbers** — doubles. `100`, `0.02`, `-3`. Time arguments may carry an `ms`
  suffix (`Wait(15ms)`); a bare number for a time is milliseconds.
- **Points** — a pair of numbers, written `Point(x, y)`. A point can be stored
  in a variable and passed to `Click` or `MoveTo`.
- **Strings** — in double quotes. `\"` and `\\` are decoded; every other
  backslash is kept as typed, so `"C:\temp\ui.bmp"` means the Windows path.

Booleans are their own small pair of keywords, `true` and `false`, and only
appear as the value of `humanize`.

`humanize=false` is the "just do it" setting: exact timing, a straight move, no
aiming spread inside `radius`/`box_w`/`box_h`, and none of the profile's
occasional cursor noise. It exists so a macro that has to hit a particular pixel
isn't quietly fighting your own humanization settings.

## Variables

```text
var target = Point(500, 300)
var distance = 40
var scaled = distance * 2 + 5
var spot = FindImage("button.bmp", tolerance=10)
Click(target)
Click(spot)
```

- `var name = Point(...)` creates a point variable.
- `var name = <expression>` creates a number variable.
- `var name = FindImage(...)` creates a point from a picture on screen.
- Reading a variable that was never set is a runtime error, not zero.

## Expressions

Numbers support `+ - * /`, unary minus, brackets, literals and number
variables. Precedence is the usual one: unary minus binds tightest, then `*`
and `/`, then `+` and `-`. Division by zero is an error. Expressions can be
used anywhere a number is expected: `Wait(base * 2)`, `loop 3 + 1`,
`Scroll(-amount)`, `Point(x + 10, y)`.

## Statements

| Statement | What it does |
|---|---|
| `profile default_sigma=<ms>` | Sets the timing spread for this script. Any other setting name is an error. |
| `var ...` | Creates a variable (see above). |
| `Wait(<expr>, humanize=true\|false)` | Waits, humanized unless told otherwise. |
| `RandomWait(<min>, <max>)` | Waits a uniformly random time in the range. |
| `MoveTo(<point>, humanize=...)` | Moves the cursor. |
| `Click(<point>, humanize=..., radius=, box_w=, box_h=)` | Moves and left-clicks, optionally at a random offset inside a disc or box. |
| `KeyDown("k")` / `KeyUp("k")` | Presses or releases one key. |
| `PressKey("k", hold_min=, hold_max=, humanize=)` | Press and release with a hold. |
| `MouseDown("b")` / `MouseUp("b")` | Holds or releases `left`, `right`, `middle`, `x1`, `x2`. |
| `Scroll(<expr>)` | Turns the wheel; negative is down. |
| `loop <expr> { ... }` | Runs the block a fixed number of times. |
| `while [not] <condition> { ... }` | Repeats while a condition holds. |
| `repeat_until [not] <condition> { ... }` | Runs once, then until a condition holds. |
| `WaitUntil(<condition>, timeout=<ms>)` | Polls a condition until it's true or the timeout expires. |
| `break` | Leaves the innermost loop. |
| `Log("message")` | Writes to the Execution Log and `logs/`. |
| `include "file.undek"` | Pastes another script in place. |
| `Call("Name", <args>...)` | Runs a function registered from C++. |
| `if [not] <condition> { ... } [else { ... }]` | Branches. |

## Conditions

`if`, `while`, `repeat_until` and `WaitUntil` take the same condition forms:

- `WindowTitle("text")` — the active window's title contains the text.
- `PixelAt(x, y) == 0xRRGGBB [tolerance <0-255>]` — a screen pixel matches a
  colour. The `==` and the colour are required, and an unreadable pixel never
  matches.
- `ImageExists("file.bmp", tolerance=<0-255>, region=Rect(x, y, width,
  height))` — a picture is somewhere on screen. Exact pixels unless a tolerance
  is given. A region limits the search to that rectangle in desktop
  coordinates, which is both faster and how you tell two identical icons apart.
  A relative picture path is looked for as written, then beside the script, then
  in the app folder — the same three places `undek check` looks, so a picture it
  accepts is one a run will load.

`not` in front of a condition flips it.

## Errors

A script is parsed completely before anything runs, so a broken script does
nothing at all. Errors carry `line N, column M:` and the parser reports as many
as it can find in one pass rather than stopping at the first.

Argument mistakes are parse errors: an unknown name (`radious=4`), a
non-boolean `humanize`, a negative size or hold, a hold above 600000 ms, or a
`hold_max` below `hold_min`. Being strict is deliberate: a silently ignored
argument looks like a working script that behaves oddly.

Runtime failures (an unsupported key, a missing template, a lost window) are
reported in the Execution Log and, where continuing makes no sense, stop the
run with the reason. Stopping the macro always releases held keys and buttons,
including one whose release during a click never confirmed.

Keys and mouse buttons are held by claim rather than by a single owner. When
two statements want the same key or button down, it stays down until the last
of them lets go: a `PressKey` inside a `KeyDown` hold leaves that hold alone,
and a `KeyUp` or `MouseUp` with another hold still open says so in the log
instead of releasing it. A `Click` is a whole press and release of its own, so
while the left button is held it is skipped and the log says why rather than
ending the hold.

The 600000 ms hold limit is the ceiling after humanization too: a profile can
stretch a hold, but never past it.

## Limits

The profile caps how much a script is allowed to do: `max_loop_iterations`,
`max_total_instructions` and `max_runtime_sec` (0 means no cap). Waits,
humanized paths and image searches check the runtime cap while they run, not
only between statements, so a runaway script stops quickly.

A `loop` count is clamped to plus or minus a billion before the iteration cap
is applied, so a count written past that is capped like any other scale of
script and never wraps around. A count of zero or less runs the block zero
times.

A single `Wait` is held to 86400000 ms (a day) by the parser and by the
runtime, and a `RandomWait` past it is refused when it is about to sleep. A
duration that large is a mistake in the script, so it is reported instead of
slept.

## Stability

The syntax has been stable since 0.10 (expressions) and 0.25 (argument
validation). New statements and arguments are additive; when a script written
for an older version no longer parses, that's a regression. There is no version
header in a script file, so avoid relying on anything documented here as
"probably fine" — if it isn't on this page, treat it as internal.
