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


def compile_module_mac (pypy_mode):
  def try_compile (d, v):
    def find_include (d):
      subs = os.listdir(os.path.join(d), "include")
      if len(subs) == 1: return os.path.join(d, "include", subs[0])
      return None
    incd = find_include()
    if not incd: return False

    for compiler in "clang++ g++ c++".split():
      compile_c = [compiler] + "-O0 -std=gnu++11 -fPIC -shared -dynamic".split()
      compile_c += ["-I", incd]
      compile_c += ["-L", os.path.join(d, "lib"), "-l", "python"+v]
      if pypy_mode: compile_c += ["-DPYROGUE_PYPY_COMPATIBLE"]
      compile_c += "pytest_module.cpp -o pytest_module.so".split()

      if subprocess.run(compile_c).returncode == 0: return True

    return False

  dirs = [
    ("/System/Library/Frameworks/Python.framework/Versions/2.7", "2.7"),
    ("Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.7", "3.7"),
    ("Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.8", "3.8"),
    ]

  for d,v in dirs:
    if os.path.isdir(d):
      if try_compile(d, v): return True

  return False


def compile_module_posix (pypy_mode):
  def try_compile (flags):
    for compiler in "clang++ g++ c++".split():
      compile_c = [compiler] + "-O0 -std=gnu++11 -fPIC -shared".split()
      compile_c += flags.split()
      if pypy_mode: compile_c += ["-DPYROGUE_PYPY_COMPATIBLE"]
      compile_c += "pytest_module.cpp -o pytest_module.so".split()

      if subprocess.run(compile_c).returncode == 0: return True

    return False

  include = [
    "-I /usr/include/python2.7",
    "-I /usr/include/python3.6",
    "-I /usr/include/python3.7",
    "-I /usr/include/python3.8",
    ]

  try:
    out = subprocess.run(["pkg-config","python","--cflags"], stdout=subprocess.PIPE).stdout
    out = out.strip()
    if out: include.insert(0, out)
  except Exception:
    pass
  try:
    out = subprocess.run(["pkg-config","python3","--cflags"], stdout=subprocess.PIPE).stdout
    out = out.strip()
    if out: include.insert(0, out)
  except Exception:
    pass


  for inc in include:
    if try_compile(inc): return True

  return False


def compile (pypy_mode):
  try:
    print("Attempting to build Python module...")

    compile_r = ("../../Programs/RogueC/RogueC-Linux --debug --api --essential "
                 "--target=Python ../Python/pytest.rogue".split())

    require(subprocess.run(compile_r).returncode == 0, "Executing Rogue compiler")

    if sys.platform == "darwin":
      require(compile_module_mac(pypy_mode))
    elif os.name == "posix":
      require(compile_module_posix(pypy_mode))
    else:
      errmsg("Unknown OS -- skipping Python tests")
      sys.exit(0)

    return True
  except:
    errmsg("Exception setting up Python tests")
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

def test (name, pypy_mode, *names):
  global current_mode
  ok = False
  fail = False
  skip = 0
  if pypy_mode != current_mode:
    if not compile(pypy_mode):
      errmsg("SKIPPING -- Couldn't compile")
      return
    current_mode = pypy_mode
  for py in names:
    exec_c[0] = py
    print("*** Trying", name, "using", exec_c[0], "(PyPy-compatible) ***" if pypy_mode else "***")
    try:
      if subprocess.run(exec_c).returncode == 0:
        passed.append(name + (" pypy-compat" if pypy_mode else ""))
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
  if fail: failed.append(name + (" pypy-compat" if pypy_mode else ""))
  if skip == len(names):
    skipped.append(name + (" pypy-compat" if pypy_mode else ""))


test("python2", False, "python2", "python2.7")
test("python3", False, "python3")
test(sys.executable, False, sys.executable)
test("python2", True, "python2", "python2.7")
test("python3", True, "python3")
test("pypy2", True, "pypy2", "pypy")
test("pypy3", True, "pypy3")
test(sys.executable, True, sys.executable)


print("*** Python test results ***")

for x in passed:
  print(" PASS:", x)
for x in skipped:
  print(" SKIP:", x)
for x in failed:
  print(" FAIL:", x)


if not passed:
  sys.exit(1)
