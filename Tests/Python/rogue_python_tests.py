from __future__ import print_function
import unittest
#from pytest import *

def msg (*args):
  #print(*args)
  pass


def _configure ():
  import pytest
  g = globals()
  for k in dir(pytest):
    if k not in g:
      g[k] = getattr(pytest, k)
  global f, b
  f = Foo()
  b = f.new_bar("MyBar")

class TestPythonBasics (unittest.TestCase):
  @classmethod
  def setUpClass (cls):
    _configure()


  def test_rogue_globals (self):
    # Test Rogue globals

    # Global routines are easily publically accessible
    self.assertEqual(rogue_routine(), 121)

    # Properties are accessible using Global singleton
    self.assertEqual(Global.singleton.rogue_global, 122)


  def test_global_methods (self):
    # Test Global methods

    self.assertEqual(f.gf_square(5), 25)
    self.assertEqual(Foo.gf_square(5), 25)


  def test_rogue_object_properties (self):
    # Test accessing Rogue properties from Python

    self.assertEqual( f.prop_str, "hello" )
    self.assertEqual( f.prop_int, 42 )
    self.assertIs( f.prop_bar, None )

    f.prop_str = "world"
    f.prop_int = 100
    f.prop_bar = b

    self.assertEqual( f.prop_str, "world" )
    self.assertEqual( f.prop_int, 100 )
    self.assertIsInstance(f.prop_bar, Bar)
    self.assertEqual( str(f.prop_bar), "<Bar MyBar>")

    f.prop_bar = Bar("PyBar")

    self.assertEqual(str(f.prop_bar), "<Bar PyBar>")

    f.prop_bar = f.prop_bar

    self.assertEqual(f.f_ss("hello"), "HELLO")


    f.prop_bar = None
    self.assertIs(f.prop_bar, None)

    f.prop_str = None
    self.assertIs(f.prop_str, None)


class TestPythonBindingsCalls (unittest.TestCase):
  @classmethod
  def setUpClass (cls):
    _configure()


  def test_rogue_function_objects (self):
    # Test calling Rogue function objects from Python

    func = f.f_sf("cap this")
    #msg(func)
    self.assertEqual(func(), "CAP THIS")


  def test_rogue_calling_python (self):
    # Test calling from Rogue into Python...

    def pyfunc_v_i ():
      r = 99
      msg("Python in pyfunc_v_i, returning " + str(r))
      return r

    def pyfunc_s_i (s):
      r = int(s) * 10
      msg("Python in pyfunc_s_i, returning " + str(r))
      return r

    def pyfunc_s_o (s):
      r = Bar("Py"+s)
      msg("Python in pyfunc_s_o, returning " + str(r))
      return r

    def pyfunc_i_s (i):
      r = "!" * i
      msg("Python in pyfunc_i_s, returning " + str(r))
      return r

    f.prop_f_v_i = pyfunc_v_i
    f.prop_f_s_i = pyfunc_s_i
    f.prop_f_s_o = pyfunc_s_o
    f.prop_f_s_b = pyfunc_s_o
    f.prop_f_i_s = pyfunc_i_s
    self.assertTrue(f.call_funcs())


class TestPythonBindingsParameters (unittest.TestCase):
  @classmethod
  def setUpClass (cls):
    _configure()


  def test_optional_arguments (self):
    # Test optional arguments

    self.assertEqual(f.f_opt_i(), 4)
    self.assertEqual(f.f_opt_i(4), 16)

    self.assertEqual(f.f_opt_s(), "hi")
    self.assertEqual(f.f_opt_s("bye"), "bye")
    self.assertIs(f.f_opt_s(None), None)


  def test_overloading (self):
    # Test overloading

    self.assertEqual(f.f_over(), "Nothing")
    self.assertEqual(f.f_over(32), "Int32")
    self.assertEqual(f.f_over("x"), "String")
    self.assertEqual(f.f_over(None), "String")
    self.assertEqual(f.f_over(32,"x"), "Int32,String")
    self.assertEqual(f.f_over(32,None), "Int32,Object")
    self.assertEqual(f.f_over(32,b), "Int32,Object")


class TestPythonBindingsExceptions (unittest.TestCase):
  @classmethod
  def setUpClass (cls):
    _configure()


  def test_rogue_code_exception (self):
    # Test Rogue code that raises exception
    self.assertRaisesRegex(RuntimeError, ".*This is from Rogue.*", f.f_rogue_exception)


  def test_python_code_exception (self):
    # Test Rogue calling Python function that raises exception

    def pyfunc ():
      raise RuntimeError("This is from Python")

    f.prop_f_exception = pyfunc

    s = f.f_python_exception()
    self.assertTrue("This is from Python" in s)


if __name__ == "__main__":
  unittest.main()
