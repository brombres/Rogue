Rogue
=====
- v1.0.54
- April 13, 2016

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

###v1.0.55 - April 15, 2016
- [API] Made functional programming more flexible.  In addition to the in-place list modification methods `apply()`, `filter()`, `modify()`, and `sort()`, there are now versions of those methods that return a modified list instead: `applying()`, `filtered()`, `modified()`, and `sorted()`.  Note: `filtered()` is functionally equivalent to `choose()` and so the latter method has been removed.
- [RogueC] Added method `Type.inject_method(Method)` that can be used to add a new method after the type has already been organized, correctly adjusting inherited method tables in extended classes.
- [RogueC] Creating an object as a standalone statement now omits the post-`init_object()` typecast which prevents a C++ warning.

###v1.0.54 - April 13, 2016
- [RogueC] Reworked native properties to be stored with regular properties instead separately.  Fixes severe bug where classes with native properties were not polymorphically compatible with base classes or extended classes due to the order in which native properties were written out.
- [Rogue] A value being explicitly or implicitly converted (cast) now checks for to-type constructor methods as well as from-type conversion methods.  So for `x:FromType -> ToType", the compiler first checks for a method `x->ToType` and then checks for `ToType.init(FromType)` or `ToType.create(FromType)`, resulting in a conversion to `ToType(x)` if that method is found.
- [RogueC] Fixed some lingering references where global methods were called "routines" (their old name).

###v1.0.53 - April 11, 2016
- [API] Added `Global.on_exit(Function())` that stores the given function object and calls it when a program ends normally or ends due to a `System.exit(Int32)` call.
- [Rogue] Added JSON-style literal syntax to create PropertyList and PropertyTable values as follows:
    - `@{ key1:value1, key2:value2, ... }` creates a PropertyTable object.  The keys do not require quotes.
    - `@[ value1, value2, ... ]` creates a PropertyList object.
    - Lists and tables may be nested; omit the leading `@` on nested structures.
- [API] Changed PropertyValue etc. `get_integer()` and `get_real()` to `get_int32()` and `get_real64()`.
- [API] Changed PropertyValue etc. `get_integer()` and `get_real()` to `get_int32()` and `get_real64()`.
- [API] Reworked printing infrastructure of PrintWriter, Global, and Console.  `Global` and `Console` are both printwriters now, with `Console` being set as the default `Global.standard_output`.  In addition to `println(value)` you can now print to the console directly with `Console.println(value)`.
- [API] The Global singleton now flushes the console output buffer on exit.
- [API] Modified LineReader to not return an extra blank line if the very last character was '\n'.  That logic was a side effect of a verbatim string printing issue that has since been separately addressed.

###v1.0.52 - April 10, 2016
- [Rogue] Added preliminary introspection support. `@TypeName` is a convenience method for calling `System.type_info("TypeName")` that returns a `TypeInfo` object with a `name` property.  You can call `.create_object()->Object` on a TypeInfo object, cast the Object to a specific type, and manually call one of the specific type's `init()` methods.
- [Rogue] Added `Object.type_info()->TypeInfo` that returns the runtime type (AKA "Class") of an object.
- [Rogue] C++ now has a function `RogueType_type_info(RogueType*)->RogueTypeInfo*` that returns the runtime type info of an object.
- [RogueC] CmdBinary and CmdUnary now call organize() on the operand types before proceeding to ensure that operator methods can be found if they exist.

###v1.0.51 - April 6, 2016
- [Rogue] `assert` now accepts an optional custom message: `assert(condition||message)`.
- [Rogue] `assert(condition[||message])` may now be used as part of expression - for example, `q = a / assert(b)`.  `b` is returned if it is "truthy" or if `--debug` is not enabled.
- [Rogue] Added `require(condition[||message])` that works just like `assert` except that it is always compiled in and does not depend on `--debug` mode.  A failed requirement will throw a `RequirementError`.
- [Rogue] Added class and method level `unitTest...endUnitTest` support (also single-line `unitTest ...`).  Tests are compiled and run at launch if `--test` is passed to `roguec` and if the containing class is used in the program.
- [Rogue] Added `[propagated]` method qualifier.  Propagated methods are cloned each time they are inherited so that their `this` reference has the correct type.  This is primarily useful when implementing the Visitor pattern.
- [RogueC] Implemented the Visitor pattern for misc. tasks.  Removed `CmdAugment*.rogue` in favor of `TraceUsedCodeVisitor.rogue` and `UpdateThisTypeVisitor.rogue` - much, much, simpler.
- [RogueC] Tweaked CPPWriter's `print_arg()` to automatically cast the arg type to the param type if they differ.
- [Standard Library] Used `--requisite` to find and fix some errors that had crept in due to other revamps.

###v1.0.50 - April 5, 2016
- [Rogue] Added `assert(condition)` statement.  When `--debug` is enabled an AssertionError will be thrown if the condition is false.  When `--debug` is not enabled the assert is stripped during compilation.
- [C++] `RogueObject_to_string(obj)` can now be called to invoke any object's `to->String()` method.  If `to->String()` was culled during compilation then the object's class name will be returned.  Exception `to->String()` methods are automatically marked `[requisite]`.
- [C++] The `to->String()` of any uncaught exceptions is now displayed at runtime.
- [RogueC] Nil returns in `init()` methods are now automatically converted to `return this`.
- [RogueC] In mixed Int32-Character operations, Int32 is now promoted to Character rather than vice versa.  Before, `Character(-1) > 0)` returned `false` even though Character is unsigned because it was being converted back to Int32 during the comparison.
- [Syntax] Updated Vim and Sublime Text 3 syntax files.

###v1.0.49 - April 4, 2016
- [Standard Library] Fixed bugs in `real_bits()` and `integer_bits()`.

###v1.0.48 - April 4, 2016
- [Standard Library] - Moved core functionality for primitive `real_bits()` and `integer_bits()` out of global create methods and into those methods themselves.
- [Standard Library] - Primitives can now be created and cast with constructor-style syntax, e.g. `n = Int32(3.4)`.
- [Standard Library] - Printing an out-of-range Character code now prints a question mark (?).
- [RogueC] - Stand-alone ranges (outside of forEach loops) now accept a `step` size.
- [RogueC] - If a range has no explicit step size then its default step type now matches the range type.

###v1.0.47 - April 2, 2016
- [Standard Library] - Added `String.up_to_first()` and `.up_to_last()` methods that are inclusive versions of `.before_first/last()`.
- [C++] - Fixed literal string generation to prevent ambiguous hex escapes, e.g. "\x9CCool" is now "\x9C" "Cool"
- [C++] - Renamed RogueString's `previous_byte_offset` and `previous_character_index` to be `cursor_offset` and `cursor_index`.
- [C++] - Added new method `RogueString_set_cursor(THIS,index)->RogueInt32` that takes a character index, sets the cached `cursor_offset` and `cursor_index` in the string, and returns `cursor_offset`.

###v1.0.46 - April 1, 2016
- Converted String and StringBuilder to use utf8 bytes instead of a Character[] array internally while using character indices externally.
- Changed Character to be an unsigned 32-bit value.
- All string indexing and substring operations operate on whole characters (code points 0 through 10FFFF); characters never span multiple indices though the internal UTF8 representation has a variable size.
- Added classes `UTF8Reader:Reader<<Character>>` and `UTF8Writer:Writer<<Character>>` that wrap `Byte` readers and writers to provide UTF-8 decoding and encoding.
- `File.reader()` and `File.writer()` now return raw byte readers and writers while `File.character_reader()` and `File.character_writer()` return UTF-8 readers and writers.
- Fixed named parameters to work with routines.
- Removed [foreign] qualifier on String.
- Renamed C++ method `RogueString_create_from_c_string()` to `RogueString_create_from_utf8()`.
- Removed C++ method `RogueString_to_c_string()`; a RogueString's `utf8` property is a null-terminated UTF-8 string that can be cast directly to `const char*`.


###v1.0.45 - March 26, 2016
- Fixed `Table` crash when `remove()` is called with a key that hashes to an unused bin.

###v1.0.44 - March 24, 2016
- Restored default `==` and `!=` functionality to compounds.  No operator methods are required for equivalence comparisons.

###v1.0.43 - March 23, 2016
- During call resolution, candidate methods not containing any given named args as parameters are discarded early.
- Fixed additional PROPERTIES in compounds to be initialized correctly.
- Compounds now generate an appropriate error message when operator methods are required but undefined (such as `operator==`).

###v1.0.42 - March 23, 2016
- Fixed runtime instanceOf to return `false` instead of `true` for null references.
- Compounds can now be declared without properties.

###v1.0.41 - March 22, 2016
- Routines can now be overloaded.
- Augments now work correctly again - augments to all specialized types (`Table`) and to specific specialized types (`Table<<String,Int32>>`) are now applied correctly.
- Fixed mysterious "unexpected end of line" error relating to augments - augments use special tokens indicating the beginning of augment code.  Those tokens were labeled "end of line" (for some reason) and their application was slightly messed up.

###v1.0.40 - March 20, 2016
- Changed C++ name adaptation syntax: `TemplateName<<Alpha,Beta>>` now becomes `TemplateName_Alpha_Beta_` and `TypeName[]` becomes `TypeName_List`.

###v1.0.39 - March 20, 2016
- Added default `init()` to base class Object.
- Classes can no longer be instantiated with a default initializer if they have other initializers all with one or more args.
- Added `requisite` target to `Makefile` that recompiles with the `--requisite` flag.
- Fixed bug where create() method candidates could be prematurely discarded from consideration in favor of `init()` methods with the same signature.
- Tweaked the C++ name adaptation and validation to make it more robust and reduce the chances of conflict.  `TemplateName<<Alpha,Beta>>` now becomes `Template$Alpha_Beta$` and `TypeName[]` becomes `TypeName$List`.

###v1.0.38 - March 19, 2016
- Added `block`/`endBlock` control structure that scopes local variables and supports `escapeBlock`.

###v1.0.37 - March 18, 2016
- Improved call resolution.
- For a call to `NewType(x)`, methods named `init()` and global methods named create() are all considered simultaneously instead of in two separate steps as before.
- If all else fails during call resolution the compiler will try and convert the arg of a 1-arg call `to->Object`, `to->String`, or `to->ParameterType` in that order (missing from previous release but present before that).

###v1.0.36 - March 18, 2016
- Fixed overly liberal method selection so that for e.g. constructor `NewType(String)`, `create(String)` will be matched before `init(Int32)` (this latter wasn't throwing an error due to String having a `to->Int32` method).
- Fixed runtime `as` and `instanceOf` to work with more than direct parent class checks.
- Moved creation of compiler's `flat_base_types` to be on-demand when generating code.

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

