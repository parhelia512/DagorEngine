# Quirrel benchmarks

Single home for all interpreter benchmarks. Sources are grouped per language:

- `quirrel/` - Quirrel workloads (`*.nut`), plus prebuilt `sq3-64.exe`
  (Squirrel 3.2 baseline) used by the docs suite
- `lua/` - Lua workloads, plus prebuilt `lua.exe` (5.5.1) and `luajit.exe`
  (2.1 rolling), which every harness runs with `-joff`
- `luau/` - Luau ports of the docs workloads, plus prebuilt `luau.exe` (0.735)
- `js/` - JavaScript ports, plus prebuilt `qjs.exe` (QuickJS-ng 0.16.2)

Quirrel itself is not committed as a binary: both harnesses run
`tools/util/sq-64.exe`, the release build from this tree (`jam -sConfig=rel` in
`prog/tools/sq`). That is the shipped runtime
configuration (clang, mimalloc); a build on the CRT heap, such as a plain cmake
`sq.exe`, is up to 2x slower on table-sweep workloads (50k-table particles sweep:
0.20s vs 0.11s), which misrepresents shipped performance.

## Rebuilding the committed interpreters

Each one is a release console exe that imports KERNEL32 alone, so a folder holds
the exe and the workloads and nothing else. All of them come from the upstream
release sources, built with the clang-cl in `devtools/LLVM-21.1.8` against the
`vc2022_17.14.4` headers, `/O2` and the static CRT (`/MT`; cmake builds take
`-DLUAU_STATIC_CRT=ON` and `-DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG"`).
LuaJIT is the exception: `src/msvcbuild.bat static` with cl, which is the
upstream Windows recipe, and `static` is what drops `lua51.dll`.

Two of them must be asked for their jump-table interpreter loop, because both
gate it on `__GNUC__`, which clang-cl does not define:

- Lua takes `-DLUA_USE_JUMPTABLE=1`, the override its own guard provides.
- QuickJS has no such knob, so the line that reads
  `#if defined(EMSCRIPTEN) || defined(_MSC_VER)` in `quickjs.c` becomes
  `#if defined(EMSCRIPTEN) || (defined(_MSC_VER) && !defined(__clang__))`.
  Without it the primes workload takes 2x longer, which measures the MSVC
  command line rather than QuickJS.

LuaJIT keeps the one source change the earlier binary carried, and the perf notes
page has the diff: `os.clock()` reads the performance counter, because the CRT
`clock()` ticks at 1 ms, which is the whole time a short rep takes.

Two harnesses run different slices of these sources:

## benchmarks.py - cross-language documentation suite

Compares Quirrel, Squirrel 3.2, Lua 5.5, LuaJIT (-joff), Luau and QuickJS on
the classic workloads (nbodies, particles, fib, primes, dict, darg, ...).
Each workload times itself (best of 10-20 in-process iterations) and prints
`"<name>", <seconds>, <iterations>`. queen solves the board 50 times inside one
timed run and reports the time of a single board, because one board takes about
as long as the coarsest clock here ticks: 20 timed runs of 50 boards keep the row
at about a second per interpreter and still resolve it to a percent. Results are
written to `../doc/content/_bench.json`, which the documentation's Performance page
renders; commit it when refreshing published numbers.

Every interpreter is pinned to one logical CPU. The mask is set on the harness,
which each interpreter inherits at creation; `--cpu N` picks the core (2 by
default, a P-core on Intel hybrid parts, where they come first) and `--no-pin`
measures without one. The core alone decides tens of percent: n-bodies takes
0.87s on a P-core, 1.34s with the sibling hyperthread busy, 1.88s on an E-core.

Each cell is `--runs` whole processes and the fastest is published. Best of N
iterations inside one process cannot see a process that was slow for its whole
life, and what makes a whole process slow - a shared core, the code alignment that
process happened to get - only adds time, so the fastest run is the reproducible
one; run_vm_bench.py publishes the median of its runs instead. A cell whose runs
spread more than 5% says so, as long as they also differ by more than 5 ms: on a
row of tens of milliseconds the clock alone is worth percents. Such a row is worth
measuring again on a quiet machine.

    python benchmarks.py             # run everything, update the doc side-car
    python benchmarks.py -l Quirrel-4.38.0 -t queen sort   # subset
    python benchmarks.py -r /tmp/try.json   # trial run, side-car untouched

Refresh a renamed interpreter with a full run, not with `-u`: `-u` keeps the rows
this run did not measure, so the old label would stay on the page beside the new
one. `../doc/gen/check.py` fails on exactly that. `-u` also writes this run's
machine and run count over every row it keeps, so it warns when the kept rows were
measured with another CPU, platform or `--runs`; a changed machine or protocol
wants a full run.

When the interpreter version changes, bump the Quirrel row label in
benchmarks.py so committed results state what was measured.

## run_vm_bench.py - VM acceptance harness

Head-to-head Quirrel (release sq) vs Lua and Luau (-O2) on paired workloads with
identical algorithms (`quirrel/<name>.nut` and `lua/<name>.lua`): general
interpreter loads (fib, binarytrees, life, mandel, strings) and daRg-UI-shaped
loads (desc_churn, probe_storm, nullable_probe, closure_storm, method_calls).
Workloads self-report five in-process reps as `BENCH <name> <ms>` lines; the
harness takes the best rep per process, median over `--runs` processes, and
prints a ratio table. Run it before and after any interpreter change.

    python run_vm_bench.py                   # measures tools/util/sq-64.exe
    python run_vm_bench.py --sq <other.exe>  # or any other build

Lua/Luau default to the prebuilt binaries above. A snapshot of results is
committed at `../doc/content/_vm_bench.json`, which the Performance page renders.
