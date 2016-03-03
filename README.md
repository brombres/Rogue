Rogue
=====
- v1.0.7
- March X, 2016

## Installation (Mac, maybe Linux)
    git clone git@github.com:Plasmaworks/Rogue.git
    cd Rogue
    make

**NOTES**

1. Running make will compile the `roguec` compiler from C++ source and create the executable script `/usr/local/bin/roguec`

2. Run `roguec` by itself for options.

3. Execute these shell commands for a simple test:

        echo println '"Hello World!"' > Hello.rogue
        roguec Hello.rogue --execute

## About
The Rogue programming language is the successor to Slag (active years 2008-2011) and Bard (never released; multiple internal versions made in 2011-2014).

## License
Rogue is released into the Public Domain under the terms of the [Unlicense](http://unlicense.org/).


## Change Log

###v1.0.7 - March X, 2016
- Added `digit_count()->Int32` to all primitives that returns the number of characters that are required to represent the primitive in text form.
- Added `whole_digit_count()` and `decimal_digit_count()->Int32` to `Real64` and `Real32` primitives.
- Parser bug fix: class auto-initializer properties can contain EOLs.
- Improved macro method error messages.
- Fixed several File macro methods that were incorrectly defined with a `return` keyword.

###v1.0.6 - March 1, 2016
- Added native code marker support for $(var.retain) and $(var.release) that yield the value of the given variable while increasing or decreasing the reference count of regular objects, preventing them from being GC'd while they're stored in a native collection.  The markers yield the variable value with no reference count modifications for primitives and compounds.

###v1.0.5 - March 1, 2016
- Added additional native code insertion marker operations for native properties and inline native code.

  1. `$this`, `$local_var_name`, and `$property_name` are supported as before.
  2. Parens may be added for clarity: `$(this)` etc.
  3. `$(this.type)`, `$(local_var_name.type)`, and `$(property_name.type)` all print out the C++ type name of the given variable.  Parens are required in this case.
  4. `$($SpecializerName)` prints out the C++ type name of a template type specializer.  For instance, in `class Table<<$KeyType,$ValueType>>`, referring to `$($KeyType)` in your inlined native code would write `RogueInt32` for a `Table<<Int32,String>>`, etc.
    

###v1.0.4 - February 29, 2016
- Fixed off-by-one error in List.remove_at() that was causing seg faults on Linux.
- Updated Makefile.

###v1.0.3 - February 29, 2016
- Renamed `Time.current()` to `System.time()` and removed the `Time` class.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

###v1.0.2 - February 29, 2016
- Adjacent string literal concatenation now works for strings that were template specializers.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

###v1.0.1 - February 28, 2016
- Adjacent string literals are now concatenated into a single string literal.

###v1.0.0 - February 28, 2016
- First full release.

###v0.0.1 (Beta) - May 12, 2015
- Initial beta release.

