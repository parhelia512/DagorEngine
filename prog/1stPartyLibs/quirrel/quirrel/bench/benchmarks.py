import os
import sys
import subprocess
import json
import platform
import time
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
# The doc site reads this side-car and renders the bars; see the {{benchmarks}}
# directive in ../doc/gen/render.py.
RESULTS = os.path.normpath(os.path.join(HERE, "..", "doc", "content", "_bench.json"))

PINNED_CPU = 2
RUNS_PER_CELL = 3
SUSPECT_SPREAD_RATIO = 0.05
SUSPECT_SPREAD_SECONDS = 0.005

lua = ["Lua-5.5.1", "lua", ["lua.exe"]]
luajit_joff = ["LuaJIT2.1-joff", "lua", ["luajit.exe", "-joff"]]
quickjs = ["QuickJS-ng-0.16.2", "js", ["qjs.exe"]]
# the Quirrel row runs the release sq built from this repo (jam -sConfig=rel in
# prog/tools/sq) - the shipped runtime
# configuration (clang, mimalloc). A dev build carries the asserts and dlmalloc
# and is up to 2x slower on table-sweep workloads, misrepresenting shipped
# performance.
SQ_REL = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "..",
                                       "tools", "util", "sq-64.exe"))
quirrel = ["Quirrel-4.38.0", "quirrel", [SQ_REL]]
squirrel = ["Squirrel-3.2", "quirrel", ["sq3-64.exe"]]
daslang_int =  ["Daslang (interperter)", None, None]
luau = ["Luau-0.735", "luau", ["luau.exe"]]

# Every interpreter the suite measures. ../doc/gen/check.py reads this to catch a
# row whose label was bumped here but left stale in the committed side-car.
ALL_LANGS = [lua, luajit_joff, quickjs, quirrel, squirrel, luau]

featured_lang = quirrel[0]
baseline_lang = None

benchmarks = [
  ("n-bodies", [
    [lua, "nbodies.lua"],
    [luau, "nbodies.luau"],
    [luajit_joff, "nbodies.lua"],
    [quickjs, "nbodies.js"],
    [quirrel, "nbodies.nut"],
    [squirrel, "nbodies.nut"],
  ]),
  ("particles-kinematics", [
    [lua, "particles.lua"],
    [luau, "particles.luau"],
    [luajit_joff, "particles.lua"],
    [quickjs, "particles.js"],
    [quirrel, "particles.nut"],
    [squirrel, "particles.nut"],
  ]),
  ("exp-loop", [
    [lua, "exp.lua"],
    [luau, "exp.luau"],
    [luajit_joff, "exp.lua"],
    [quickjs, "exp.js"],
    [quirrel, "exp.nut"],
    [squirrel, "exp.nut"],
  ]),
  ("dictionary", [
    [lua, "dict.lua"],
    [luau, "dict.luau"],
    [luajit_joff, "dict.lua"],
    [quickjs, "dict.js"],
    [quirrel, "dict.nut"],
    [squirrel, "dict.nut"],
  ]),
  ("darg-ui-benchmark", [
    [lua, "darg.lua"],
    [luajit_joff, "darg.lua"],
    [luau, "darg.luau"],
    [quickjs, "darg.js"],
    [quirrel, "darg.nut"],
    [squirrel, "darg.nut"],
  ]),
  ("fibonacci-recursive", [
    [lua, "fib_recursive.lua"],
    [luau, "fib_recursive.luau"],
    [luajit_joff, "fib_recursive.lua"],
    [quickjs, "fib_recursive.js"],
    [quirrel, "fib_recursive.nut"],
    [squirrel, "fib_recursive.nut"],
  ]),
  ("fibonacci-loop", [
    [lua, "fib_loop.lua"],
    [luau, "fib_loop.luau"],
    [luajit_joff, "fib_loop.lua"],
    [quickjs, "fib_loop.js"],
    [quirrel, "fib_loop.nut"],
    [squirrel, "fib_loop.nut"],
  ]),
  ("primes-loop", [
    [lua, "primes.lua"],
    [luau, "primes.luau"],
    [luajit_joff, "primes.lua"],
    [quickjs, "primes.js"],
    [quirrel, "primes.nut"],
    [squirrel, "primes.nut"],
  ]),
  ("float2string", [
    [quirrel, "f2s.nut"],
    [squirrel, "f2s.nut"],
    [lua,"f2s.lua"],
    [luajit_joff,"f2s.lua"],
    [luau,"f2s.luau"],
    [quickjs,"f2s.js"],
  ]),
  ("queen", [
    [quirrel, "queen.nut"],
    [squirrel, "queen.nut"],
    [lua,"queen.lua"],
    [luajit_joff,"queen.lua"],
    [luau,"queen.luau"],
  ]),
  ("sort", [
    [quirrel, "table-sort.nut"],
    [squirrel, "table-sort.nut"],
    [lua,"table-sort.lua"],
    [luajit_joff,"table-sort.lua"],
    [luau,"table-sort.luau"],
  ]),
  ("spectral-norm", [
    [quirrel, "spectral-norm.nut"],
    [squirrel, "spectral-norm.nut"],
    [lua,"spectral-norm.lua"],
    [luajit_joff,"spectral-norm.lua"],
    [luau,"spectral-norm.luau"],
    [quickjs,"spectral-norm.js"],
  ]),
  ("string2float", [
    [quirrel, "f2i.nut"],
    [squirrel, "f2i.nut"],
    [lua,"f2i.lua"],
    [luajit_joff,"f2i.lua"],
    [luau,"f2i.luau"],
    [quickjs,"f2i.js"],
  ])
]


def pin_self_and_children(cpu):
  if sys.platform == "win32":
    from ctypes import WinDLL, WinError, c_size_t, get_last_error, wintypes
    k32 = WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = wintypes.HANDLE
    k32.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, c_size_t]
    if not k32.SetProcessAffinityMask(k32.GetCurrentProcess(), 1 << cpu):
      raise WinError(get_last_error())
  else:
    os.sched_setaffinity(0, {cpu})


class pushd:
    def __init__(self, path):
        self.olddir = os.getcwd()
        if path != '':
            os.chdir(os.path.normpath(path))
    def __enter__(self):
        pass
    def __exit__(self, type, value, traceback):
        os.chdir(self.olddir)

def isnumeric(f):
  try:
    float(f)
  except ValueError:
    return False
  return True

def suspect_spread(times):
  fastest, slowest = min(times), max(times)
  ratio = slowest / fastest - 1
  if slowest - fastest < SUSPECT_SPREAD_SECONDS or ratio < SUSPECT_SPREAD_RATIO:
    return 0
  return ratio


def header_mismatch(kept, runs):
  now = machine_info()
  claimed = (("runs", kept.get("runs"), runs),
             ("cpu", kept.get("info", {}).get("cpu"), now["cpu"]),
             ("platform", kept.get("info", {}).get("platform"), now["platform"]))
  return [name for name, before, after in claimed if before != after]


def measure_once(cmds, folder):
  try:
    with pushd(folder):
      proc = subprocess.run(cmds, capture_output=True,text=True, check=True)
      out = proc.stdout
  except subprocess.CalledProcessError as e:
    print("Error", e, f"\nin {folder} performing {cmds}")
    return None
  fullout = out
  out = out.splitlines()
  if len(out)==0:
    print(f"Error in {cmds}, no correct output, got {fullout}, expected <test name>, <testres in float seconds>, <number of tests>")
    return None
  out = out[0].split(",")
  out = [str(o).strip() for o in out]
  possible_vals = [o for o in out if isnumeric(o)]
  if len(possible_vals) > 0:
    possible_val = possible_vals[0]
  else:
    possible_val = out[0]
  return float(possible_val) if isnumeric(possible_val) else possible_val


def run_tests(benchmarks, results_file_name=None, perform_tests=None, perform_tests_by_name=None, update=False,
              runs=RUNS_PER_CELL):
  res = {}
  if update:
    # -u keeps the rows this run does not measure. A row whose label changed since
    # is kept too, so refresh a renamed interpreter with a full run instead.
    with open(results_file_name, "rt", encoding="utf-8") as f:
      kept = json.load(f)
    res = kept.get("results", {})
    stale = header_mismatch(kept, runs)
    if stale:
      print("WARNING: the kept rows were measured with a different", ", ".join(stale),
            "and the header written now speaks for them too, so refresh them as well")
  for test_name, benchs in benchmarks:
    if perform_tests_by_name is not None and test_name not in perform_tests_by_name:
      continue
    rs = res.get(test_name, {})
    res[test_name] = rs
    for data, file in benchs:
      if perform_tests is not None and data not in perform_tests:
        continue
      lang_name, folder, cmds = data
      cmds = cmds + [file]
      folder = os.path.join(HERE, folder)
      # newer Windows Python does not resolve bare exe names against the
      # child cwd; the interpreters live next to their scripts
      exe = os.path.join(folder, cmds[0])
      if os.path.exists(exe):
        cmds = [exe] + cmds[1:]
      print(cmds, folder)
      times = [measure_once(cmds, folder) for _ in range(runs)]
      if None in times:
        continue
      timed = all(isinstance(t, float) for t in times) and min(times) > 0
      val = min(times) if timed else times[0]
      if runs > 1:
        print("  ", times, "->", val)
        spread = suspect_spread(times) if timed else 0
        if spread:
          print(f"     suspect: the {runs} runs spread {spread * 100:.0f}%, re-measure this row")
      rs[lang_name] = val
  write_results(results_file_name, res, runs)
  return res

def write_results(results_file_name, res, runs=RUNS_PER_CELL):
  """The side-car the doc site renders: the numbers, the row to pick out, and what
  they were measured on."""
  doc = {"info": machine_info(), "featured": featured_lang, "runs": runs, "results": res}
  with open(results_file_name, "w", encoding="utf-8", newline="\n") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")

def machine_info():
  """What the numbers were measured on. run_vm_bench.py imports this too, so the
  two side-cars describe their machine the same way."""
  return {
    "platform": f"{platform.platform()} ({platform.release()})",
    "arch": platform.machine(),
    "cpu": platform.processor(),
    "when": time.strftime("%Y-%m-%d"),
  }

if __name__ == "__main__":
  def_perform_tests_for_langs = ALL_LANGS
  def_langs = [l[0] for l in def_perform_tests_for_langs]
  def_tests = [b[0] for b in benchmarks]
  parser = argparse.ArgumentParser(description='Script to do benchmarks.')
  parser.add_argument('-l','--lang', type=str, nargs='+', default = def_langs, help='langs to perform tests. default are:' + ",".join(def_langs))
  parser.add_argument('--update', '-u', default=False, help='keep the rows this run does not measure', action='store_true')
  parser.add_argument('-t','--test', type=str, nargs='+', default = def_tests, help='test to do. default are all, Possible options:' + ",".join(def_tests))
  parser.add_argument('-r','--result', type=str, default = RESULTS, help='json file for results; a path of your own leaves the committed side-car alone')
  parser.add_argument('--cpu', type=int, default = PINNED_CPU, help=f'logical CPU every interpreter is pinned to, default {PINNED_CPU}')
  parser.add_argument('--no-pin', dest='pin', default=True, action='store_false', help='do not pin, which lets a row drift by tens of percent')
  parser.add_argument('--runs', type=int, default = RUNS_PER_CELL, help=f'process runs per cell, the fastest is published, default {RUNS_PER_CELL}')
  args = parser.parse_args()
  if args.pin:
    pin_self_and_children(args.cpu)
    print("pinned to cpu", args.cpu)
  perform_tests_for_langs = [l for l in def_perform_tests_for_langs if l[0] in args.lang]
  run_tests(benchmarks, args.result, perform_tests_for_langs, args.test, update=args.update, runs=args.runs)
  print("results ->", args.result)
