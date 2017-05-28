# Run with:
#
#   python makedoc.py *.rogue | pbcopy


import sys
import cgi


def out (fmt = "", *args, **kw):
  r = []
  for a in args:
    r.append(cgi.escape(a))
  if kw.pop("nonl",False):
    print fmt % tuple(r),
  else:
    print fmt % tuple(r)

def tuo (fmt = "", *args, **kw):
  r = []
  for a in args:
    r.append(str(a))
  if kw.pop("nonl",False):
    print fmt % tuple(r),
  else:
    print fmt % tuple(r)

def is_section (l):
  for w in ["DEFINITIONS", "METHODS", "ENUMERATE", "PROPERTIES", "GLOBAL PROPERTIES", "GLOBAL METHODS"]: # Is that all?
    if l == w: return True
  return False

def process (next_line, filename, url_prefix=None):
  num,line = next_line()
  mtype = ""
  while line is not None:
    save_line = False
    l = line.strip()
    if l.startswith("class "):
      out("=== %s ===", l)
      #if (url_prefix): tuo("[%s/%s#L%s &#128279;]", url_prefix, filename, num, nonl=True)
    elif l == "endClass":
      pass
    elif l.startswith("method "):
      tuo("%s <tt>%s</tt>", mtype, l[7:])
      if (url_prefix): tuo("[%s/%s#L%s &#128279;]", url_prefix, filename, num)
      out("<br />")
    elif l == "METHODS":
      mtype = "method"
    elif l == "GLOBAL METHODS":
      mtype = "global method"
    elif l == "PROPERTIES" or l == "GLOBAL PROPERTIES":
      m = l.lower()[:-3] + 'y'
      while True:
        num,line = next_line()
        if line is None: break
        l = line.strip()
        if is_section(l) or l == "endClass":
          save_line = True
          break
        tuo("%s <tt>%s</tt>", m, l)
        out("<br />")
    elif l.startswith("routine "):
      out("== %s ==", l)
    if not save_line:
      num,line = next_line()


def reader (source):
  num = 0
  for line in source:
    num += 1
    if not line: continue
    if line.strip().startswith("#"): continue
    yield num,line
  while True:
    yield num,None



# e.g., --url=https://github.com/AbePralle/Rogue/blob/master/Source/Libraries/Standard/
url_prefix = None
if sys.argv[1].startswith("--url="):
  url_prefix = sys.argv[1][6:]
  url_prefix = url_prefix.rstrip("/")
  del sys.argv[1]

out("__TOC__")
out()

for filename in sys.argv[1:]:
  out("== %s ==" % (filename,))
  process(reader(open(filename, "r").read().split("\n")).next, filename, url_prefix)
