Rogue
=====
- v1.0.35
- March 18, 2016

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

###v1.0.35 - March 18, 2016
- Reworked debug trace infrastructure; now substantially faster and more flexible - 1.3s vs 1.7s roguec recompile in debug mode.
- Debug Stack Trace now shows accurate line numbers.
- Fixed access resolution error where class `Global` could be checked before the current class context.
- Fixed several errors resulting from compiling with `--requisite`.

###v1.0.34 - March 18, 2016
- C++ exceptions and stdlib improvements.
- Fixed access resolution error where class `Global` could be checked before the current class context.
- Fixed several errors resulting from compiling with `--requisite`.

###v1.0.33 - March 17, 2016
- Aspects are now compatible with Object, instanceOf Object, and can be be cast to Object in more places.
- Added fallback property getters and setters to aspects.

###v1.0.32 - March 17, 2016
- Added `consolidated()->String` method to String and StringBuilder.  Operates similarly to Java's intern() and maps equivalent strings to a single string object instance.
- Compiler now generates global properties for native classes and initializes them with the class's `init_class()` call.
- Class File now includes native header `<cstdio>` when referenced.

###v1.0.31 - March 16, 2016
- Renamed the methods of the plug-in architecture.

###v1.0.30 - March 16, 2016
- Added beginnings of compiler plug-in infrastructure.
- Added `--version` directive.
- Added explicit `[native]` attribute to type Object - the compiler was already doing so internally.
- Removed unused, vestigial copy of global properties generated as static variables in class definitions.

###v1.0.29 - March 15, 2016
- Improved `select{}` syntax - `||` is used to separate all parts (`select{ a:x || b:y || c:z || d }`) and `x` may be used instead of both `x:x` and `x.exists:x:value` (if `x` is an optional type).
- Added *up to less than* range operator `..<`.  For instance, `forEach (i in 0..<count)` loops from `0` to `count-1`.

###v1.0.28 - March 14, 2016
- When used in implicit typing, routine calls now correctly report the return type instead of the type of the routine wrapper class.

###v1.0.27 - March 14, 2016
- Added `String.contains(Character)->Logical`.
- Renamed `String.pluralize()` to `String.pluralized()` for consistency.
- Moved `Boss.rogue` to its own library to avoid cluttering up the Standard library API.
- Overridden methods are now culled when they're unused.

###v1.0.26 - March 14, 2016
- Fixed `[requisite]` routines to work correctly when the routine has a return type.
- Renamed `[functional]` to `[foreign]`.  A `[foreign]` class is one that supplements a native non-Rogue reference type with methods callable from Rogue.  This feature is still somewhat hard-coded for the C++ implementation of Rogue `String`.
- Fixed method overriding to make sure the overriding method's return type is instanceOf the overridden method's return type instead of short-circuiting when the overridden method is abstract.
- Tightened up `reset()` and `seek()` in various readers and writers, fixing a few potential bugs in the process.

###v1.0.25 - March 13, 2016
- All compound/primitive/functional types now have default `to->Object` returning Boxed<<$DataType>>.
- Routine [attributes] now apply to the routine as well as the method.
- stdlib.System: Expose GC threshold; make overridable
- stdlib.System: Improve `run()` method to throw error on failure and return exit code.
- stdlib.File: Fixes and improvements
- stdlib.Object: Include address in default `to->String`
- stdlib.PrintWriter: Flush more often
- Added .abs() to Real and Int primitives.
- Renamed Math.ceil() to ceiling().
- `$requisite ...` can now be specified inside a class or method to only take effect if the class or method is used.
- Removed conversion of optional reference types to plain references to allow dependable functionality (e.g. boxing) to be present in class Optional<<$DataType>>.
- Improved error message when illegally naming a global method `init()`.
- Changed some vestigial references of "routines" to be "global methods" instead in error messages.
- Fixed new side effect bug resolving statement list of `Global.on_launch()`.  Was not expecting list to be modified while looping but the type-level "inner requisite" directive causes new CmdMakeRequisite nodes to be added to `on_launch`.  Resolving list would create new types which would add additional commands to list.

###v1.0.24 - March 11, 2016
- Added '--gc=boehm' support for Boehm's garbage collector, courtesy Murphy McCauley.
- Added '--gc-threshold={number}[MB|K]' and increased default to 1MB.
- Changed the allocation tracker to count down from the threshold to 0 instead of counting up from 0 to the threshold.
- Fixed bug mapping variable writes to `set_` methods.

###v1.0.23 - March 10, 2016
- Integrated MurphyMc's gc option which allows GC to happen during execution rather than only between calls into the Rogue system.
- Fixed a reference holding error in List.

###v1.0.22 - March 9, 2016
- `++` and `--` now work on compounds by transforming `++compound` into `compound = compound.operator+(1)` and `--compound` into `compound.operator+(-1)`.
- Added full set of `operator` methods to `Degrees` and `Radians` compounds.  Also added `Degrees.delta_to(other:Degrees)` which returns the smallest angle (which may be positive or negative) required to to from this angle to the specified other angle.  For instance, `Degrees(270).delta_to(Degrees(0))` returns `Degrees(90)`.
- Added an elapsed time output when a compile is successful.
- Lists now have a default capacity of zero to begin with and do not allocate a backing array.  When the first item is added an initial capacity of at last 10 is reserved.

###v1.0.21 - March 8, 2016
- Changed syntax of `select{ condition:value, default_value }` to `select{ condition:value || default_value }`.
- Removed support for compound modify-and-assign operators since they're only changing a stack copy of the compound.  Rogue now automatically converts "compound += value" to "compound = compound + value".

###v1.0.20 - March 8, 2016
- `operator+=` and other modify-and-assign operator methods now work with compounds as well as regular class types.
- `System.environment` provides several ways to operate environment variables: `.listing()->String[]`, `get(name:String)->String`, and `set(name:String,value:String)`.  A small example:

    System.environment[ "STUFF" ] = "Whatever"

    forEach (var_name in System.environment.listing)
      println "$ = $" (var_name,System.environment[var_name])
    endForEach

    System.environment[ "STUFF" ] = null


###v1.0.19 - March 8, 2016
- Aspect types now correctly extend RogueObject in C++ code.

###v1.0.18 - March 8, 2016
- `RogueObject_retain()` and `RogueObject_release()` now guard against `NULL`.
- Renamed `STL` library to `CPP`.

###v1.0.17 - March 7, 2016
- Objects requiring clean-up are no longer freed if they have non-zero reference counts.

###v1.0.16 - March 7, 2016
- Removed trailing whitespace in all source files.

###v1.0.15 - March 7, 2016
- Task system now works again.  Any custom main loop should call `Rogue_update_tasks()` which returns `true` if tasks are still active for `false` if they've all finished.  `Rogue_update_tasks()` automatically runs the garbage collector if required.
- Fixed a bug in `await` implementation by adding an internal `yield` that was missing.
- Added RogueCallback infrastructure for taking native actions before or after runtime events.  The first two supported callbacks are `Rogue_on_begin_gc` and `Rogue_on_end_gc`.
- Added Math.abs().

Task system example:

    Tests()
    println "Tests started!"

    class Tests
      METHODS
        method init
          run.start

        method run [task]
          print_numbers( 1, 10 ).start
          await print_numbers( 10, 13 )
          await print_numbers( 100, 103 )
          print_numbers( 1000, 1003 ).start

        method print_numbers( low:Int32, high:Int32 ) [task]
          forEach (n in low..high)
            println n
            yield
          endForEach
    endClass


###v1.0.14 - March 7, 2016
- Renamed `IntX.as_realx()` and `RealX.as_intx()` to be `IntX.real_bits()` and `RealX.integer_bits()`.
- Fixed GC code generation for compound properties.
- Fixed `local x = x` to throw a syntax error instead of crashing the compiler.
- Fixed compiler crash when parsing a super-minimal class definition such as "class Name;".
- Compiler now ensures that 'as' is only used with references.

###v1.0.13 - March 5, 2016
- Fixed bug where listing a base class after an aspect would corrupt the dynamic dispatch (vtable) ordering.

###v1.0.12 - March 5, 2016
- Made several empty `Reader<<$DataType>>` methods abstract so they don't overwrite existing methods when incorporated in an extended class.

###v1.0.11 - March 5, 2016
- Fixed bug in aspect call mechanism.
- Added `List.choose(Function($DataType)->Logical)->$DataType[]` that returns a list subset.  Example:

    println Int32[][3,4,5,6].choose( function(n)=(n%2==0) )

- Added `StackTrace.to->String` in addition to the existing `StackTrace.print()`.



###v1.0.10 - March 5, 2016
- Added call tracing when the `--debug` option is given.
- Created StackTrace() class and added it to base Exception class.  If `--debug` is enabled then you can `ex.stack_trace.print` when catching an exception.  Seg faults (from referencing null) do not throw exceptions but the stack trace will still print out if `--debug` is enabled.
- Fixed bug when DEFINITIONS have more than one token.
- Tweaked default Reader `seek()` and `skip()` methods.

###v1.0.9 - March 4, 2016
- Fixed up inline native definitions and methods to work with op-assign and increment/decrement.
- Added convenience class `ListLookupTable<<$KeyType,$ValueType>>` where assigning a value to a key adds the value to a list of values instead of replacing a single value.
- Reworked template overloading system.
- Added Real64/32 `is_infinite()`, `is_finite`, `is_number()`, and `is_not_a_number()/is_NaN()` as well as literal values `infinity` and `NaN`.  StringBuilder now prints those values correctly.
- Can now write `@routine_name` to obtain a Function object that calls the routine.
- Added STL library containing `STLPriorityQueue`.
- Added `Set<<$DataType>>` to standard library.


###v1.0.8 - March 3, 2016
- 'select' now uses curly braces instead of square brackets.
- The final case in a `select` can be optionally prefixed with `others:`.  Example: https://gist.github.com/AbePralle/de8e46e025cffd47eea0
- Added support for template overloads (same name, different numbers of specializers).
- Bug fix in template backslash parsing.

###v1.0.7 - March 3, 2016
- Added the `select` operator, Rogue's alternative to the ternary "decision operator".  Example: https://gist.github.com/AbePralle/de8e46e025cffd47eea0
- Added `digit_count()->Int32` to all primitives that returns the number of characters that are required to represent the primitive in text form.
- Added `whole_digit_count()` and `decimal_digit_count()->Int32` to `Real64` and `Real32` primitives.
- Parser bug fix: class auto-initializer properties can contain EOLs.
- Improved macro method error messages.
- Fixed several File macro methods that were incorrectly defined with a `return` keyword.
- Conditional compilation now respects nativeHeader and nativeCode blocks.
- nativeHeader and nativeCode blocks can appear at the global level, the class section level, or the statement level.  In the latter two cases they are only applied if the class or method is referenced, respectively.
- Fixed generated trace() code to work for arrays of compounds that don't contain references.

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

