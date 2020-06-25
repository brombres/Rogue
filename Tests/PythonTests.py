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


def compile_module_mac ():
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


def compile_module_posix ():
  def try_compile (flags):
    for compiler in "clang++ g++ c++".split():
      compile_c = [compiler] + "-O0 -std=gnu++11 -fPIC -shared".split()
      compile_c += flags.split()
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


try:
  print("Attempting to build Python module...")

  # Change into script's directory
  os.chdir(os.path.dirname(os.path.realpath(__file__)))
  os.chdir("Build")

  compile_r = ("../../Programs/RogueC/RogueC-Linux --debug --api --essential "
               "--target=Python ../Python/pytest.rogue".split())

  require(subprocess.run(compile_r).returncode == 0, "Executing Rogue compiler")

  if sys.platform == "darwin":
    require(compile_module_mac())
  elif os.name == "posix":
    require(compile_module_posix())
  else:
    errmsg("Unknown OS -- skipping Python tests")
    sys.exit(0)

  print("Executing Python tests...")

  pp = os.environ.get("PYTHONPATH", "")
  if pp: pp += ":"
  pp += os.getcwd()
  os.environ["PYTHONPATH"] = pp

  exec_c = [sys.executable, os.path.join(os.getcwd(), "..", "Python", "rogue_python_tests.py"), "-v"]
  os.execv(exec_c[0], exec_c)

  # Shouldn't reach this!
  errmsg("Test didn't execute")
  sys.exit(1)

except:
  errmsg("Exception during Python testing")
  sys.exit(1)
