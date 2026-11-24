# Changelog

Version numbers follow the usual `major.minor.patch` reading: patch releases fix
things, minor releases add things, and 1.0 is the first release I'm willing to
call finished.

## 1.29.0

- **The window draws its own title bar.** The editor is frameless now: no system
  caption, and the name, the drag area and the minimize, maximize and close
  buttons are drawn in ImGui along the top of the menu bar. A drag hands the
  move to Windows, so snapping, double-click to maximize and the drop shadow
  still work, and a drag started straight after the previous one is picked up
  as readily as the first. The window keeps its taskbar button, maximizing
  stops at the work area instead of covering the taskbar, and the corners are
  rounded while it isn't maximized, without moving its bounds. The three
  buttons act when they are released, like the system's own. Minimize follows
  the minimize to tray setting and otherwise leaves the window minimized on the
  taskbar, and close follows close to tray.
- **The macro list lines up.** A macro with a binding sat in a taller row than
  one without, and its name started a few pixels further right, because the
  checkbox is a whole frame high and the empty space left where one would be
  was neither as wide nor as tall. Every row now reserves that frame height, so
  the names share one left edge and the rows are evenly spaced.
- **The builder's loop count is readable.** The number sat in whatever width
  the two step buttons left over, which was barely wider than the digit itself,
  so it looked clipped and pushed to one side. It has a box of its own now, the
  value sits in the middle of it, and the box widens with the digits instead of
  cutting them off.
- **A window's title bar can be seen.** The bar of the settings, hotkey,
  debugger and builder windows was the same shade as the panel under it, so the
  bar you drag or close them by disappeared into the window. It has a colour of
  its own now, brighter again while that window has focus.

## 1.28.2

- **An include is looked for beside the script.** `include "shared.undek"` only
  searched the working folder and the app folder, so a file kept next to the
  macro failed to load even though a picture beside it was found. The parser now
  takes the script's folder, tries that first, and a nested include looks beside
  the file it came from.
- **A hotkey run finds the macro's own folder.** Starting a binding never passed
  the script's folder to the runner, so `ImageExists("btn.bmp")` worked from the
  editor and reported "not on screen" from a hotkey. Both paths pass it now.
- **A `PixelAt` colour can't be cast out of range.** `PixelAt(0, 0) == 1e300`
  converted a double an `unsigned int` can't hold, which is undefined behaviour
  from a line of script. A colour outside `0x000000` to `0xFFFFFF` is refused
  where it is parsed, the same way the other limits are.

## 1.28.1

- **A wait can't be infinite.** A seconds value large enough that multiplying
  it by 1000 overflowed was accepted and stored as an infinite duration, and
  the sleep loop then subtracted 25 ms from infinity without ever making
  progress, so the run sat there until Stop. The lexer turns such a number away
  now, a literal `Wait` is held to 86400000 ms (a day), and the sleep itself
  refuses anything past that, which also catches a duration computed from an
  expression or a `RandomWait`.
- **The editor checks a profile before it uses it.** Loading a profile ignored
  both the load result and `validate()`, so a hand edited file with an inverted
  range reached the preview path generator. Loading now goes into a copy and is
  only applied when it validates, with the reason in the status bar. Saving
  checks the same thing, so a profile the runtime would refuse can't be written
  out.
- **A `while` counter can't overflow at the cap.** The counter was incremented
  before it was compared, so a `max_loop_iterations` of `INT_MAX` overflowed it
  on the way past the cap. It is compared first now.

## 1.28.0

- **A loop count can't narrow into a negative one.** A literal past `INT_MAX`
  was converted to `int` before the iteration cap was applied, so
  `loop 2147483648` became a huge negative count and ran zero times. Literals
  and expressions now share one limit, so both are clamped the same way and the
  cap is what decides how often the loop runs.
- **Profile numbers are checked before they are converted.** Integer settings
  such as `steps_per_100px` or `max_loop_iterations` went through `get<int>()`,
  which truncates a fractional value and wraps one too large for an `int`. A
  profile with either is rejected as a whole now, and the profile in use is left
  alone.
- **A pause can't outlive its run.** Pressing pause in the moment between a run
  ending and the editor noticing could set the flag just after cleanup had
  cleared it, so the next run started paused. Pause requests now go through the
  run slot under the same lock as the end of a run, and a late one is refused.
- **The ImGui download is hash checked.** The archive was pinned to a tag but
  not to its contents, unlike the JSON header. It has the same SHA256 check
  now.

## 1.27.0

- **Cleanup releases each held input once.** A key or button can be both
  confirmed held and possibly held, and the two lists were released one after
  the other, so the same physical input got two releases. Cleanup now releases
  the union of both lists, which can't eat a real press in whatever comes next.
- **A failed `PressKey` can't release another statement's hold.** The press
  that never confirmed still ran the release bookkeeping, so with the failure
  policy on continue it could end a `KeyDown` hold and drop its claim. It only
  compensates for a press of its own now, and leaves a key another statement
  already holds alone.
- **A `Click` waits while the left button is held.** A click is a press and a
  release of its own, and the release used to end a `MouseDown` hold that still
  owned the button. Such a click is skipped and the log says why, for humanized
  and plain clicks alike.
- **The recorder distance check can't overflow.** The gap between two moves was
  squared in `long long`, which the full coordinate range overflows. It is
  measured with `std::hypot` now.
- **Humanization can't stretch a hold past the limit.** `hold_scale_max` and
  `hold_panic_mult_max` had no upper bound, so a profile edited by hand could
  turn an accepted hold into days. Both are capped at 10, and the sampled hold
  is clamped to the 600000 ms limit after scaling.
- **Visual builder improvements.** The deletion prompt is centred. `Use cursor`
  waits for a click on the spot you want instead of taking the button's own
  coordinates. The pixel check has a `Pick` button that fills in the position
  and colour in one click. The three wrap buttons line up in one column,
  `Insert into script` is coloured like the other main actions, steps can be
  moved up and down, and `Top level` is disabled at the top and highlighted
  when steps are going into a block.

## 1.26.0

- **A click that couldn't let go is cleaned up.** The click paths retried a
  release that failed, but when both releases lost, the button was reported as
  failed and forgotten. Cleanup only knows about buttons the script asked for,
  so a left button left down stayed down. A failure now says whether the press
  might still be held, and such a button goes on the cleanup list before the
  run stops.
- **More than one statement can hold the same key.** `KeyDown("w")` followed
  by `PressKey("w", ...)` ended with the press releasing the key, so the hold
  `KeyDown` asked for was gone for the rest of the script. Two `KeyDown`s with
  one `KeyUp` between them had the same problem, and mouse buttons worked the
  same way. Every press and hold now claims the key or button, it stays down
  while any claim is left, and only the last one sends the release. A `KeyUp`
  or `MouseUp` that leaves another claim open says so in the log.
- **Holds have a top end.** A press could ask for `hold_min=1e300`, which
  reached the scheduler's clock conversion and wrapped. Holds are now refused
  above 600000 ms, and the scheduler cuts any wait past 1e9 ms and ignores a
  value that isn't a number, so a runtime limit isn't the only thing keeping
  the conversion in range.
- **The recorder's move spacing can't overflow.** The distance between two
  moves was squared in an `int`, which a long jump could overflow and turn into
  a small number. It is worked out in `long long` now.
- **The `FunctionRegistry` rules mention threads.** The comment said lookups
  are locked; it now also says a copy outlives a removal on another thread and
  that no lookup sees half an entry.

## 1.25.0

- **A refused `PressKey` is let go of once.** When the desktop turned the
  press down, the release after it was never reported back, so the key stayed
  on the list of things cleanup releases. The run ended with an extra release
  for a key that was already up, which can eat a real press of that key in
  whatever comes next. A release that works now takes the key off the list
  straight away.
- **`undek --version > NUL` keeps quiet.** The app looked for a console by
  file type alone, and NUL is a character device like a console is, so it
  opened one and printed there. It asks `GetConsoleMode` now, which only a real
  console answers, so `> NUL` and `2> NUL` discard the output as asked.
- **The hints in Settings fit inside the window.** The two long ones under the
  stealth hotkey and the presets wrapped off the right edge; they wrap onto a
  second line now, and the window starts a little wider.
- **One path preview control instead of two.** The toolbar had a button that
  plotted the next move and a checkbox for the pane; ticking Show path now does
  the plotting, so the button is gone.
- **The floating windows can't be collapsed.** Settings, Hotkey Bindings and
  the Debugger used to fold down to their title bars on a stray double click;
  the triangle is gone.
- **Settings says where the profile starts.** A separator and heading now come
  before the humanization sliders, so it reads as one file saved by Save profile
  rather than as part of the window's own hotkey settings.
- **The script keeps its minimum height in a short window.** The log's two
  spacing gaps weren't counted when the layout worked out how small the script
  could get.

## 1.24.0

- **A `PressKey` that can't let go is cleaned up like any other press.** The
  statement never added its key to the list of things held
  for the run, so a release the desktop refused ended there: the macro stopped,
  cleanup released what it knew about, and that key stayed down for whatever you
  did next. The key is on the list from the moment it's sent now, one refused
  release is retried, and one that still fails is attempted again on the way
  out.
- **A collapsed log can be brought back.** Dragging the splitter down put the
  log away and stretched the script into the space the splitter had, which left
  nothing to grab; the splitter keeps its strip. Ticking the Log checkbox also
  clears the collapse instead of showing an empty strip.
- **The window, the tray and the installer use the app's own icon.** They used
  to fall back to the generic Windows one.
- **`undek --version 2> errors.txt` keeps stderr in the file.** Opening a console
  for the windowless app repointed both streams whenever stdout wasn't
  redirected; each stream is now asked separately and only the one that needs a
  console gets one.

## 1.23.1

- **Output from the command line goes where you send it.** `undek --version > version.txt`,
  or piping a `run` into something, used to open a console window for the app and
  put the text there, which left the file empty. When stdout is already redirected
  the app now writes to it instead of opening a console of its own.

## 1.23.0

- **A refused key or click stops the macro now.** Input failures used to be
  logged and stepped over, so `KeyDown("w")` could be dropped and the script
  still waited out the hold, moved the mouse and pressed keys as if `w` were
  down, finishing with a clean-looking log and none of the intended work. A
  failed press, release, move, click or scroll now stops the run with the
  reason in the log and the error dialog. A profile can set
  `"input_failure_policy": "continue"` for the old log-and-carry-on behaviour.
- **Held keys are only the ones that actually went down.** The runtime used to
  count a key as held before the backend answered, so a refused press could be
  released later as though it had landed. A press is now recorded only when the
  backend confirms it, and one that was refused is still released on the way
  out in case it half landed.
- **The log no longer pushes the script off a short window.** Its minimum height
  and the script's now shrink together when there isn't room for both, instead
  of overflowing the editor, and dragging the splitter all the way down puts
  the log away entirely. Double-clicking the splitter brings it back.

## 1.22.0

- **Help → Scripting reference opens something now.** It used to hand the README
to Windows and walk away, and on a machine with no program registered for
`.md` files that produced nothing at all. The menu now checks the file is
really there, opens it in Notepad when the shell refuses it, and says so in the
status bar if neither works. If the README is missing entirely it opens the
copy on GitHub instead of doing nothing.
- **The rest of the Help menu.** There's a Language reference item (the full
grammar), Open macros folder, Open examples folder, Keyboard shortcuts, Report
an issue, and F1 as a shortcut for the reference. `docs/` ships with the
release now, so the README's own link to it isn't dangling after an install.
- **The shortcuts are listed in the app.** Help → Keyboard shortcuts shows
Run, Dry-run, Debug, breakpoints, stepping, Save, New and every global hotkey
exactly as they're currently bound, so the answer doesn't need a trip to the
README.
- **New no longer eats unsaved edits.** Ctrl+N, File → New macro and the sidebar
button now go through the same Save / Discard / Cancel prompt as picking a
another macro, and a save that fails leaves you where you were instead of
quietly moving on.
- **Shortcuts wait for dialogs.** F5 and friends no longer start a run behind an
open confirmation popup, and the pause hotkey is unregistered on shutdown like
the others.
- **The macro list shows the right binding.** A binding used to be matched by
looking for the filename anywhere in the stored path, so `cat.undek` could
pick up `bobcat.undek`'s hotkey.

## 1.21.0

- **The Execution Log can be resized, and starts shorter.** The strip between
  the script and the log can be dragged, and double-clicking it puts the log
  back to its default height. That default is a little smaller than before, so
  a long script isn't squeezed by an empty log pane.
- **Macro file actions moved to right-click.** Duplicate, Rename and Delete are
  on each entry in the macro list now instead of a button row that took two
  lines of the sidebar, and they only act on the macro you selected, so a
  mis-click can't rename the wrong file.
- **The actions that start something have a colour.** Record is a muted red, New
  a blue, Run a green, and Save turns green while the buffer has unsaved
  changes. Everything else keeps the plain grey, so the sidebar reads at a
  glance.
- **The status bar stops repeating itself.** MACROS ON/OFF was already a big
  button at the top of the sidebar, so the bottom bar now shows what is
  happening right now: idle, running or paused, the active profile, and the
  cursor position.
- **Menus say what they do.** Refresh macro list moved from View to File, Path
  preview is one View toggle instead of a View toggle and a Run command, and the
  toolbar and sidebar buttons have tooltips that name their hotkey.
- **A Help menu.** Scripting reference opens the README that ships with the app,
  GitHub repository opens the project page, and About shows the version and
  where macros and examples live.

## 1.20.0

- **A pause pressed as the run starts is no longer swallowed.** Starting a run
  used to clear the pause flag, and the worker reached that line a moment after
  the hotkey did, so a pause pressed too quickly vanished and the macro kept
  going. The pause now belongs to the run it was pressed during and is cleared
  when that run ends, which is also what keeps the next run from inheriting it.
- **A long `Wait` hears the pause.** Waits are slept in short hops and checked
  between them, so a pause lands within a few dozen milliseconds instead of
  after the full delay. A key that is being held, or a cursor partway along a
  humanized path, still finishes that action first, because stopping halfway
  through one would leave it half-done.
- **`check` now reads calls against what the function declared.** A call with
  the wrong number of arguments, or text where the function asked for a number,
  is reported with its line before anything runs, instead of failing once the
  run reaches it. A number expression counts as a number without being worked
  out, since checking has no variables to read.
- **Checking and running look for a picture in the same places.** Both go
  through one resolver now: the path as written, then beside the script, then in
  the app folder. Before this, a picture kept next to a macro passed `check`
  and then failed to load at run time, which made the check wrong in the one
  case it was there to catch.

## 1.19.0

- **A script can be checked without running it.** The Run menu and `undek check
  <file>` report parse errors, calls to functions nothing has registered, and
  pictures that are missing or aren't a BMP it can read, each with its line.
  None of that needs a screen, so the command works in CI and on a script you
  aren't ready to run yet.
- **A running macro can be paused and picked up again.** `Ctrl + Shift + P`
  (rebindable in Settings) holds the run at its next step; pressing it again
  carries on. A pause set before the run starts holds the first step, a new run
  doesn't inherit an old pause, and Stop still ends a paused run rather than
  waiting for a resume.

## 1.18.0

- **Image searches can be limited to a rectangle.** `ImageExists("shop.bmp",
  region=Rect(0, 0, 960, 540))` and the same on `FindImage` only look inside that
  part of the screen, given in desktop coordinates like everything else. Scanning
  a 4K desktop for a 20-pixel icon was most of what a picture loop cost, so this
  is the difference between a loop that keeps up and one that doesn't; it's also
  the way to tell two identical icons in different windows apart. A region that
  hangs off the edge of the screen searches the part that's still on it.

## 1.17.1

- **Looking at the screen keeps the picture for a few milliseconds.** Two checks
  in a row (`if ImageExists(a) {} if ImageExists(b) {}`) used to capture the whole
  desktop twice from the same instant, and a loop with no wait in it could do it
  thousands of times a second. A capture now lasts 10 ms, and sending any input
  throws it away straight away, because that is the thing that changes the screen.

## 1.17.0

- **Two copies of the editor can't run at once any more.** Each copy drove the
  same cursor and neither knew about the other, so a second launch could fire
  macros into whatever the first was doing. Starting the editor again brings the
  running window forward instead. The command line is exempt, so a script can
  still be checked while the editor is open.
- **Recording is refused while a macro is running.** Strokes coming back out of
  the Interception driver look exactly like hardware, so a take started mid-run
  captured the macro's own keys along with yours.
- **Macro names are turned into file names Windows will actually accept.** Only
  spaces and separators used to be replaced, so a name with `?`, `:`, `*` or a
  trailing dot failed to save and the macro simply didn't appear.

## 1.16.0

- **A pixel nobody can read is no longer reported as black.** Off Windows the
  sampler used to answer 0, which is indistinguishable from a genuinely black
  pixel; it says "unreadable" now and the condition is false. Tests inject their
  own sampler, so a colour condition behaves the same wherever it runs instead
  of depending on what happens to be on screen.
- **The colour picker refuses rather than inventing a colour.** A failed read
  used to put `0x000000` into the script and the clipboard as if it had been
  sampled, leaving a `PixelAt(...)` condition that could never be true. It now
  says the pixel couldn't be read and inserts nothing.
- **Rebuilding the hotkey registry says which of three things happened**, rather
  than logging a failure and returning true. All registered, some taken by
  another app, or nothing usable: only the last one is fatal, which is what the
  code already meant and what the command line tool already did.

## 1.15.1

- **Recording no longer starts silently when the hooks fail.** Starting a take
  now waits for the hooks to install and reports the reason if one doesn't, so
  the status line says "Recording didn't start" instead of the editor claiming
  to record while nothing arrives. With only one hook up, the working one is
  taken down too rather than recording half a take.

## 1.15.0

- **A function's declaration is checked when it registers, not when it's called.**
  An argument type the host can't check, a return type that isn't a number or
  text, two arguments with the same name, or a required argument after an
  optional one is refused with the reason instead of quietly doing something
  else later.
- **A function that promises a number and returns text fails the call**, which
  shows up in the Execution Log like any other failure and leaves the script
  running.
- **The VM looks a function up once.** The body and the declaration now come out
  of a single locked lookup, so a registration changing on another thread can't
  hand a call mismatched halves.

## 1.14.1

- **A BMP can't point its pixels into its own header any more.** The offset only
  had to be past 54, so a file claiming a 124-byte header could have its first
  row read out of header bytes. The pixels now have to start after the header,
  whatever size it says it is.

## 1.14.0

- **Profile values that become numbers later are capped.** A hand-edited profile
  could set `steps_per_100px` high enough that the step count overflowed an
  `int` while a path was being built. Everything numeric must now be a real
  number, and the fields that turn into an int, a duration or a tick count have
  ceilings far above anything the editor offers. Loading such a profile is
  refused with the field named.
- **The path generator clamps in floating point before rounding**, so even a
  profile that got past validation can't produce a path full of nonsense.

## 1.13.2

- **Humanized moves obey the coordinate limit.** A plain `MoveTo` already refused
  anything past a million pixels, but `humanize=true` planned a curve straight to
  it and rounded the steps into an `int` that couldn't hold them. The target is
  checked before the branch now, the scheduler refuses a path step that leaves
  the range, and a humanized click whose jitter would push it out is reported
  instead of taken.

## 1.13.1

- **Holding both Shift keys and letting go of the first one released the
  modifier early.** The take wrote `KeyUp("shift")` while the other Shift was
  still down, because the check only looked at keys that had already written a
  KeyDown. The key still held now takes the recording over and its release
  writes the KeyUp, in either order and across a pause.

## 1.13.0

- **Rarely missed inputs are on by default.** The balanced profile now drops the
  odd press at about 0.8%, which is what the shipped `profiles/default.json`
  already said, so the app and the file finally agree. The off preset still
  turns it off, and the checkbox in the profile editor is the way out.
- A test now reads the packaged profile and compares it with the defaults in
  code, so the two can't drift apart again.

## 1.12.0

- **The mouse hotkey decisions are tested.** Which message counts as a press
  (including the double-click spellings that a fast tap produces), which button
  of the two side ones it was, whether the modifiers the binding asks for are
  actually held, and how many presses the queue will keep — all of that now
  lives in `MousePressQueue` with tests behind it. The hook still lets every
  press through untouched.

## 1.11.0

- **The recorder's decision-making is testable.** Turning hook events into
  script lines now lives in `RecorderTape`, away from the low-level hook, so the
  rules that used to be checked by hand on Windows are covered: the two Shift
  keys writing one KeyDown, releases that happen while paused being written on
  resume, a key held through a whole pause staying held, mouse jitter being
  dropped, wheel deltas and the waits in between. Behaviour is unchanged.

## 1.10.0

- **A function that throws no longer ends the run.** An exception from a
  registered function is caught and logged as a failed call, which is the same
  thing as returning false, so the rest of the script still runs.
- **Registrations can be removed.** `FunctionRegistry::remove()` reports whether
  there was anything to take out, and a failed call afterwards behaves exactly
  like calling something that was never registered.
- **Lookups are locked and declarations can be copied out.** `findInfo()` gives
  a copy instead of a pointer into the registry, which could be invalidated by a
  registration on another thread. The registry header now states the rules:
  register before the run, exceptions are the host's problem, removal is for
  teardown, and `kFunctionApiVersion` is there for compatibility checks.

## 1.9.0

- **A dry run no longer opens the input backend.** It used to build the real one,
  which meant loading `interception.dll` and creating a driver context for a run
  that sends nothing; the log now says `backend: none (dry run)`.
- **A profile warning is no longer swallowed when the script succeeds.** Falling
  back to the default profile still runs, and now says so instead of printing a
  plain success line.

## 1.8.1

- **An empty hotkey in `settings.json` no longer replaces the default.** It left
  the app claiming to be enabled with a hotkey that could never be registered.
- **The toggle sound volume is kept between 0 and 100 on load**, instead of
  keeping whatever a hand-edited file said. Settings also have tests now.

## 1.8.0

- **The execution log rotates instead of growing forever.** A long session writes
  a line per statement, and the file had no limit at all. It now rolls over at
  4 MB, keeping the previous file as `undek-<date>.log.1`, so a run that goes
  wrong is still readable without the log filling the disk.

## 1.7.1

- **`interception.dll` is only loaded from the app's own folder** (or system32).
  A plain `LoadLibrary` also searches the working directory and `PATH`, so a
  copy dropped in either of those would have been loaded and run. If you relied
  on putting the DLL on `PATH`, move it next to `undek.exe`.

## 1.7.0

- **A hold is never shorter than a frame.** `Wait(0.025s, humanize=true)` between
  a button press and its release is sampled around the time you asked for, but
  the spread means roughly one hold in twelve came out under 15 ms - shorter
  than a 60 fps frame, so games polling their input simply never saw the button
  go down. That is what "it works but skips some inputs" looked like, and side
  buttons suffered most because their presses are short. Humanized holds now
  have a floor of `min_press_ms` (30 ms by default, settable in the profile
  JSON, `0` to switch it off), and the log says `(press floor)` when it applies.

## 1.6.1

- **Side buttons pass through again.** 1.6.0 stopped a bound side button's press
  from reaching the window underneath, which also swallowed the macro's own
  MouseDown("x1")/MouseDown("x2") when the Interception driver sent them: that
  driver's events reach a Windows hook looking identical to real hardware, so the
  app could not tell its own clicks from yours. Nothing is eaten now, whatever
  the backend. The double-tap fix from 1.6.0 stays.

## 1.6.0

- **Side-button hotkeys no longer miss the second tap.** A quick double tap of
  Mouse4/Mouse5 sends the second press as a `WM_XBUTTONDBLCLK` message, which the
  watcher ignored, so roughly every other fast tap did nothing. Middle, right and
  left binds had the same hole.
- **Inputs are no longer dropped by default.** The "rarely missed inputs"
  humanization option shipped switched on at 0.8%, which deliberately skipped one
  press and its release; a short script lost an input about one run in fifteen.
  It now has to be turned on, and only the Safe and Aggressive presets do.

## 1.5.0

- **A release that fails once is tried again.** A `KeyUp` or `MouseUp` that didn't
  land used to be logged and forgotten, which left that key or button held down
  for whatever you did next. It now gets the same single retry a click's release
  already had.
- **More of the awkward cases are tested.** The randomizer's clamps and rates,
  the recorded-lines round trip, the debugger stepping through a loop, and
  mutated versions of a real BMP and a real script instead of only random junk.

## 1.4.0

- **A template that isn't really a BMP is turned away before it costs anything.**
  The file's size is checked before it's read into memory, so pointing a script
  at a video or an archive by mistake no longer means a huge allocation just to
  find out it isn't a picture.
- **More broken headers are named for what they are.** A file claiming more
  header than it contains, or an impossible number of image planes, gets its own
  message instead of a vague failure.
- **Fuzzers for the parser and the BMP reader**, for anyone with clang and an
  afternoon to spare. The image reader decodes from memory now, so the fuzzer
  doesn't have to write a file for every case it tries.
- **Bytes after the last row are ignored**, which is what some screenshot tools
  leave behind, and saving a frame too large for a BMP's 32-bit size fields now
  says so instead of writing a corrupt file.

## 1.3.0

- **A click doesn't fire when the cursor never got there.** If a move fails - a
  locked desktop, a game swallowing the input - the click is skipped and the log
  says so, instead of pressing wherever the cursor happened to be sitting.
- **A throttled click retries its release.** The slow input path sent the button
  release once and gave up, which could leave the left button held down; it now
  retries once the same way the direct path always has.
- **A script can't overflow into nonsense.** Arithmetic that runs past the
  largest number the app can hold stops the script with a clear error, and
  coordinates, wheel amounts, loop counts and wait timeouts are held inside sane
  limits before they're used.
- **An overshoot stops being a certainty.** On a long move the chance could be
  pushed past 100%, which made every long hop overshoot; it's clamped now.
- **A registered function gets the arguments it asked for.** If a C++ function
  declares its arguments, a call with the wrong number of them or the wrong
  type is turned away with a log line naming the function, instead of arriving
  as a surprise.
- **Typos in a profile are caught.** An unknown `profile` setting in a script is
  an error instead of being quietly ignored, and a misspelled `input_backend` is
  refused instead of quietly acting like SendInput.

## 1.2.0

- **Waits stop burning a core.** They only spin for the last couple of
  milliseconds now instead of the last twenty, which used to keep a core busy
  while nothing was happening.
- **Image checks are cheaper.** The screen capture is reused between checks
  instead of building a new picture every time, `ImageExists` stops at the first
  match instead of scanning the rest of the desktop for a better one, and one
  template written two ways is only loaded once.
- **`PixelAt` stops grabbing a device context for every read**, which was more
  work than the read itself inside `WaitUntil` loops.
- **The log file is written, not reopened.** A run with a lot of statements used
  to open and close the file once per line.
- **The editor stops re-highlighting the whole script every frame.** Lines are
  split and measured when the text changes and reused while you're not typing.
- **The log panel and the macro filter are cached** instead of being rebuilt on
  every frame, and the log list no longer shifts a vector for every line.
- **Recording with both Shift keys down no longer writes a release that didn't
  happen**, or two presses for one key.
- **Toggle sounds don't spawn a thread each time.**
- **A folder that vanishes or is locked can't crash the macro list**, which used
  to throw out of a menu click; the listing uses the non-throwing filesystem
  calls now.
- **Deleting a macro only clears the editor if that macro was the one open.**

## 1.1.0

- **Mouse side-button hotkeys keep up now.** They were checked once per drawn
  frame, so a quick tap could land between two frames and vanish, and a press
  made while a macro was running could eat the next one. They come from a small
  mouse hook on its own thread instead, so nothing is missed and a side button
  feels like a normal hotkey. If the hook can't be installed, Undek quietly goes
  back to the old per-frame check.
- **`listen` mode fires mouse-button bindings.** It only ever watched keyboard
  hotkeys, so a `Mouse4` binding did nothing there.
- **Delete confirmation names the macro.** It says which one is going, not just
  "this macro".
- **Installer:** the Start menu shortcut is a checkbox now instead of always
  being created. It's ticked by default, and unticking it sticks across
  upgrades.

## 1.0.0

- **Installer.** `UndekMacro-1.0.0-setup.exe` installs per-user (no admin
  prompt), with optional desktop shortcut and "start with Windows". The portable
  zip still exists for people who want it.
- **First start sets itself up.** A new install gets the folders it needs, a
  default profile and bindings file, and one starter macro, then shows a short
  welcome panel. The panel appears once; nothing already on disk is overwritten.
- **Version marker.** `.undek-version` records which version last started, which
  is what makes "first start" a real question instead of a guess.
- Comments can sit back to back now, including a bare `//` line. The second
  one in a row used to become a stray token and the script failed to parse.
- The editor caret is back. It was checking whether the *gutter* was focused
  instead of the text, so neither the caret nor the Ln/Col readout ever
  updated.
- Debugger history now describes `Call(...)` statements instead of showing `?`,
  and image conditions instead of `PixelAt(0, 0)`.
- The whole tree is formatted with a pinned `clang-format`, and CI fails on
  formatting, warnings or tests under `-Wall -Wextra -Werror`.
- `docs/architecture.md` explains how a script travels from text to input.

## 0.32.0

- `Click(..., humanize=false)` now lands exactly on the point. It used to pick up
  the profile's cursor noise anyway, while ignoring `radius`/`box_w`/`box_h` for
  the same click. `humanize=false` is now consistently "do exactly this".

## 0.31.0

- Two debugger display bugs, both found by turning on warnings-as-errors.
- Formatting enforced in CI; architecture document added.

## 0.30.0

- Registered script functions can describe themselves: description, typed
  arguments, return type, version. They also get a context with cancellation,
  elapsed/remaining runtime and a log line that lands in the Execution Log.
  The old three-argument registration still works.

## 0.29.0

- The randomization tests actually test the randomization now: seeded
  reproducibility, clamp bounds, drift and noise limits, hit rates, and the hold
  envelope that lets a panic hold stretch past `hold_max`.

## 0.28.0

- The recorder tracks what is physically held separately from what it recorded,
  so pausing and stopping can't leave a key logically stuck, and inserting an
  unbalanced take is refused.

## 0.27.1

- Profiles and bindings load all-or-nothing; a rejected file leaves the current
  one in place. Saving uses a single replace step, so a crash mid-write can't
  truncate a config.
- Reloading bindings replaces them instead of adding a second copy.

## 0.27.0

- Junk input can no longer crash the lexer, parser or BMP loader: a seeded
  robustness test feeds them garbage, and CI runs the suite under ASan/UBSan.

## 0.26.0

- `docs/language.md`, and parse errors now point at a line *and* column.

## 0.25.0

- Typos are rejected instead of ignored: unknown named arguments, non-boolean
  `humanize`, negative sizes or holds, a reversed hold range and negative
  timeouts are all errors. String escapes are decoded only for `\"` and `\\`.

## 0.24.0

- Image handling got limits: oversized BMP dimensions and pixel counts are
  refused, "file isn't there" and "file isn't a picture" are reported
  differently, and a template scan checks the runtime limit as it goes.

## 0.22.2

- Image coordinates are desktop coordinates, so matching works on multi-monitor
  setups with monitors left of or above the primary one.

## 0.22.1

- Stop can't leave input held: releases skip the throttle and the cancel check,
  a failed mouse-up is retried, and every held key/button is released
  individually.
- One run at a time, with Stop never lost between runs.

## 0.22.0

- `undek --version` and `--help`, a CI check on what's inside the release
  package, and a troubleshooting section in the README.

## 0.21.0

- `Call("Name", ...)` runs a function registered in C++.

## 0.20.0

- `var p = FindImage("x.bmp")` gives you the middle of the match, ready for
  `Click(p)`.

## 0.19.0

- `ImageExists("x.bmp")` as a condition, with a per-run template cache.

## 0.18.0

- BMP loading and tolerant template matching, as pure logic with tests.

## 0.17.0

- "Sample this profile" shows what the current settings actually do to a wait.

## 0.16.0

- Profile presets: Off, Safe, Balanced, Aggressive.

## 0.15.1

- A single-run gate, so two macros can't interleave input.

## Before 0.15

Debugger with breakpoints and an execution timeline, input recorder with
pause/resume and Unicode keys, the interactive path editor, numeric variables
and expressions, the visual macro builder with nesting, `Wait`/`Scroll` taking
expressions, multi-error diagnostics, hotkeys and bindings, the tray icon and
stealth mode, and the Interception backend fallback.
