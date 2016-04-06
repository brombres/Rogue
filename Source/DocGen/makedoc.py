# Run with:
#
#   python makedoc.py *.rogue | pbcopy


import sys
import cgi


def out (fmt = "", *args):
  r = []
  for a in args:
    r.append(cgi.escape(a))
  print fmt % tuple(r)

def is_section (l):
  for w in ["DEFINITIONS", "METHODS", "ENUMERATE", "PROPERTIES", "GLOBAL PROPERTIES", "GLOBAL METHODS"]: # Is that all?
    if l == w: return True
  return False

def process (next_line):
  line = next_line()
  mtype = ""
  while line is not None:
    save_line = False
    l = line.strip()
    if l.startswith("class "):
      out("=== %s ===", l)
    elif l == "endClass":
      pass
    elif l.startswith("method "):
      out("%s <tt>%s</tt>", mtype, l[7:])
      out("<br />")
    elif l == "METHODS":
      mtype = "method"
    elif l == "GLOBAL METHODS":
      mtype = "global method"
    elif l == "PROPERTIES" or l == "GLOBAL PROPERTIES":
      m = l.lower()[:-3] + 'y'
      while True:
        line = next_line()
        if line is None: break
        l = line.strip()
        if is_section(l) or l == "endClass":
          save_line = True
          break
        out("%s <tt>%s</tt>", m, l)
        out("<br />")
    elif l.startswith("routine "):
      out("== %s ==", l)
    if not save_line:
      line = next_line()


def reader (source):
  for line in source:
    if not line: continue
    if line.strip().startswith("#"): continue
    yield line
  while True:
    yield None


out("__TOC__")
out()

for filename in sys.argv[1:]:
  out("== %s ==" % (filename,))
  process(reader(open(filename, "r").read().split("\n")).next)
