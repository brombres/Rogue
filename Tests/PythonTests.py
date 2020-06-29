from __future__ import print_function
import os
import subprocess
import sys

def require (cond, msg="Unknown"):
  if not cond:
    print("Failure: " + str(msg))
    os.exit(1)

def errmsg (*args):
  print(*args)

def strs (args):
  def fix (s):
    if isinstance(s, bytes): return s.decode("UTF-8")
    return str(s)
  return [fix(s) for s in args]


def compile_module_mac (mode):
  def try_compile (d, v):
    def find_include (d):
      subs = os.listdir(os.path.join(d, "include"))
      if len(subs) == 1: return os.path.join(d, "include", subs[0])
      return None
    if d:
      incd = find_include(d)
      if not incd: return False
    else:
      incd = None

    for compiler in "clang++ g++ c++".split():
      compile_c = [compiler] + "-O0 -std=gnu++11 -fPIC -shared -dynamic".split()
      if incd: compile_c += ["-I", incd]
      if d: compile_c += ["-L", os.path.join(d, "lib"), "-l", "python"+v]
      if mode == "pypy": compile_c += ["-DPYROGUE_PYPY_COMPATIBLE"]
      compile_c += "pytest_module.cpp -o pytest_module.so".split()

      print("Trying to compile:", " ".join(strs(compile_c)))

      if subprocess.run(compile_c).returncode == 0: return True

      errmsg("Failed to compile.")

    return False

  if mode == "pypy":
    return try_compile(None, None)

  if mode == 2:
    dirs = [
      ("/System/Library/Frameworks/Python.framework/Versions/2.7", "2.7"),
      ]
  else:
    dirs = [
      ("/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.7", "3.7"),
      ("/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.8", "3.8"),
      ]

  for d,v in dirs:
    if os.path.isdir(d):
      if try_compile(d, v): return True

  return False


def compile_module_posix (mode):
  def try_compile (flags):
    for compiler in "clang++ g++ c++".split():
      compile_c = [compiler] + "-O0 -std=gnu++11 -fPIC -shared".split()
      if flags: compile_c += flags.split()
      if mode == "pypy": compile_c += ["-DPYROGUE_PYPY_COMPATIBLE"]
      compile_c += "pytest_module.cpp -o pytest_module.so".split()

      print("Trying to compile:", " ".join(strs(compile_c)))

      if subprocess.run(compile_c).returncode == 0: return True

      errmsg("Failed to compile.")

    return False

  if mode == "pypy":
    return try_compile(None)

  include = []

  if mode == 2:
    include += [ "-I /usr/include/python2.7" ]
    try:
      out = subprocess.run(["pkg-config","python","--cflags"], stdout=subprocess.PIPE).stdout
      out = out.strip()
      if out: include.insert(0, out)
    except Exception:
      pass
  else:
    include += [
      "-I /usr/include/python3.6",
      "-I /usr/include/python3.7",
      "-I /usr/include/python3.8",
      ]
    try:
      out = subprocess.run(["pkg-config","python3","--cflags"], stdout=subprocess.PIPE).stdout
      out = out.strip()
      if out: include.insert(0, out)
    except Exception:
      pass


  for inc in include:
    if try_compile(inc): return True

  return False


def compile (mode):
  try:
    print("Attempting to build Python module...")

    compile_r = ("--debug --api --essential "
                 "--target=Python ../Python/pytest.rogue".split())

    if sys.platform == "darwin":
      compile_r = ["../../Programs/RogueC/RogueC-macOS"] + compile_r
    elif sys.platform in "linux linux2 linux3".split():
      compile_r = ["../../Programs/RogueC/RogueC-Linux"] + compile_r
    else:
      errmsg("Unknown OS -- skipping Python tests")
      sys.exit(0)

    require(subprocess.run(compile_r).returncode == 0, "Executing Rogue compiler")

    if sys.platform == "darwin":
      require(compile_module_mac(mode))
    else:
      require(compile_module_posix(mode))

    return True
  except Exception as e:
    errmsg("Exception setting up Python tests: " + str(e))
    return False



print("Executing Python tests...")

# Change into script's directory
os.chdir(os.path.dirname(os.path.realpath(__file__)))
os.chdir("Build")

pp = os.environ.get("PYTHONPATH", "")
if pp: pp += ":"
pp += os.getcwd()
os.environ["PYTHONPATH"] = pp

exec_c = [None, os.path.join(os.getcwd(), "..", "Python", "rogue_python_tests.py"), "-v"]
passed = []
failed = []
skipped = []

current_mode = None

def test (name, mode, *names):
  global current_mode
  ok = False
  fail = False
  skip = 0
  if mode != current_mode:
    if not compile(mode):
      errmsg("SKIPPING -- Couldn't compile")
      return
    current_mode = mode
  for py in names:
    exec_c[0] = py
    print("*** Trying", name, "using", exec_c[0], "in mode", mode, "***")
    try:
      if subprocess.run(exec_c).returncode == 0:
        passed.append(name + " " + str(mode))
        ok = True
        break
      else:
        fail = True
    except FileNotFoundError as e:
      if e.filename == py:
        skip+=1
        print("SKIPPING -- " + str(e))
        print()
      else:
        fail = True
    except:
      fail = True

  print()
  if ok: return
  if fail: failed.append(name + " " + str(mode))
  if skip == len(names):
    skipped.append(name + " " + str(mode))


test("python2", 2, "python2", "python2.7")
test("python3", 3, "python3")
test("python2", "pypy", "python2", "python2.7")
test("python3", "pypy", "python3")
test("pypy2", "pypy", "pypy2", "pypy")
test("pypy3", "pypy", "pypy3")
test(sys.executable, "pypy", sys.executable)


print("*** Python test results ***")

for x in passed:
  print(" PASS:", x)
for x in skipped:
  print(" SKIP:", x)
for x in failed:
  print(" FAIL:", x)


if not passed:
  sys.exit(1)
