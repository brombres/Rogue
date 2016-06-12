Rogue
=====
- v1.0.64
- June 11, 2016

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


## License
Rogue is released into the Public Domain under the terms of the [Unlicense](http://unlicense.org/).


## Change Log

###v1.0.65 - June 12, 2016
- [API] Added comparison operators for `Value` types and implemented `ValueList.contains(String|Value)`.

###v1.0.64 - June 11, 2016
- [Rogue] Renamed `CLASS` section to `DEPENDENCIES`.  Use to inject native code and mark classes and methods as requisite IF the currrent class is used in the program.
- [Rogue] Modify and assign operator (example: `+=`) now work on list and array element accesses.  Note that the element access is cloned, so `list[i] += x` becomes `list[i] = list[i] + x` and ultimately `list.set( i, list.get(i) + x )`.
- [API] Reworked `List(initial_capacity:Int32,initial_value:$DataType)` to be much faster by copying the initial value directly into the backing array instead of repeatedly adding it.
- [API] All List `discard` methods now return `this` list.
- [API] Improved `create()` and `to->String()` methods of `TimeInterval` class.
- [API] Added an optional global interface to Stopwatch.  `Stopwatch.start(key:String)` creates and starts a global stopwatch,`Stopwatch.finish(key:String)` prints the key and the elapsed time, removing the stopwatch, and `Stopwatch.remove(key:String)->Stopwatch` removes and returns the stopwatch without printing the elapsed time (you can print the elapsed time yourself if desired).
- [API] Simplified `PrintWriterAspect` now that aspect method return types of `this` are converted into the type of the incorporating class.
- [API] Added `clamped(Int32?,Int32?)` to `Real64` and `Real32` since `Int32` parameters cannot be automatically cast to `Real64?`.
- [API] Added `Real32.to->String`.
- [API] `File` tweaks - primarily renamed `absolute_filepaths` for `listing()` to be `absolute` instead etc.
- [RogueC] Fixed method template bug - methods added to already resolved classes are now resolved as well.
- [RogueC] An unresolved method call involving named args now prints out the candidate method parameter names.
- [RogueC] Compounds with native properties now produce properly compileable C++ code.
- [C++] Further modified C++ try/catch macros to allow polymorphically compatible catches.
- [C++] Renamed native array data pointers from `reals/floats/longs/integers/characters/bytes/logicals/objects` to `as_real64s/as_real32s/as_int64s/as_int32s/as_characters/as_bytes/as_logicals/as_objects`.
- [C++] Added typedef for `RogueWord` as a 16-bit unsigned integer.

###v1.0.63 - May 28, 2016
- [Rogue] Conversion methods (`method to->String`) can now accept parameters on definition (`method to->String(arg:Int32)`) and on call (`st = obj->String(x)`).
- [API] Renamed `clone()` methods to be called `cloned()` instead to fit with the general Rogue convention of verbs mutating and returning `this` object and adjectives returning a mutated copy.
- [API] Added integer primitive `to->String(&digits:Int32,&hex,&octal,&binary)` conversion methods.
- [API] Added `Date.to->String(&ymd,&hms,&s,&ms,&format=24)` conversion method.  Specifying `&format=12` or `&format=24` automatically sets the `&hms` flag to include Hours, Minutes, and Seconds in the output.
- [API] Added `Value.sort(compare_fn:Function(a:Value,b:Value))` that sorts ValueList types into order based on an arbitrary comparison function.
- [API] Added `Value.remove(key:String)->Value` to base class `Value`.  This method is overridden by `ValueTable`.
- [API] Added `List.discard(Function(value:$DataType))->this` that discards items identified by the supplied function.
- [API] Added `List.subset(i1:Int32,[n:Int32])->List`.
- [API] Added optional parameters to `String.trimmed([left:Int32],[right:Int32])->String`.  If either `left` or `right` is specified then the corresponding number of characters are trimmed off the left or right sides of the returned string.  If neither are specified then whitespace is trimmed off of both ends.
- [API] Renamed integer primitive method `to_power_of_2()` to `to_power_of_two()`.
- [RogueC] Improved no-parens args parsing.

###v1.0.62 - May 24, 2016
- [API] Renamed `get_int32()` etc. to be `int32()` for Value types (Value, ValueList, ValueTable).
- [API] Fixed `File.timestamp()` - was returning value in milliseconds instead of real seconds.
- [API] Added class `TimeInterval` that can be added to a Date value or obtained by subtracting two Date values.
- [API] Adjusted numerical primitive method `clamped(low,high)` to have use optional named parameters: `clamped(&low=null,&high=null)`.
- [API] Added `or_larger(other)` and `or_smaller(other)` to numerical primitives.  `a = a.or_larger(b)` is equivalent to `a = Math.max(a,b)`, likewise for `or_smaller` and `Math.min`.
- [API] Added `.floor()` to primitives `Real64` and `Real32`.
- [API] Added `.format([decimal_digit_count])->String` to Real64 and Real32 that returns a string representation with no more than 4 decimal digits if `decimal_digit_count` is omitted or else the exact number of decimal digits specified.
- [API] Renamed `List.filter()` and `List.filtered()` to `List.keep()` and `List.keeping()`.  The original names are still available as macros for developer convenience.
- [API] Fixed an off-by-one error in `List.from(i1,i2)` where `i2` was not being included.  This also fixes InsertionSort, which was leaving out the last element every time a sort was performed.
- [RogueC] Fixed step size to have an effect again in two forEach loop variants... had been broken since 'step' started being parsed as part of a range.
- [RogueC] Now supporting operator methods for binary operators (`&`, `|`, `~`, `:<<:`, `:>>:`, `:>>>:`).

###v1.0.61 - May 21, 2016
- [API] Renamed JSON-style data `PropertyValue`, `PropertyList`, and `PropertyTable` to be `Value`, `ValueList`, and `ValueTable` instead.
- [Rogue] The symbols `@[]` (ValueList) and `@{}` (ValueTable) can now be used as types.  For example, declaring a property `table : @{}` is equivalent to `table : ValueTable`, while `table = @{}` is equivalent to `table = ValueTable()`.
- [Rogue] Macro methods are now `[propagated]` automatically so that they resolve in the most specific object context before being substituted in place of the call.
- [Rogue] `catch` can now omit the variable name if the variable isn't used and just specify an exception type.  For example, `catch (err:Error)` can be written `catch(Error)`.
- [Rogue] Improved generated code of try/catch system to handle multiple 'catch' levels.
- [API] Fixed Value add() macro methods to return 'this' instead of Value so that they have the correct return type when called on ValueList.
- [RogueC] Exception types specified in `catch` are now marked as 'used' types in the event that those types aren't referenced anywhere else.
- [RogueC] Added `-Wall` (enable all warnings) option to clang when roguec is run with `--compile`.

###v1.0.60 - May 19, 2016
- [Rogue] Added weak references (note: only tested in manual GC mode).  Create a `WeakReference<<$DataType>>(obj)` and then access
its `value` as desired.  The weak reference does not prevent the contained object from being collected and, if it is, the weak reference's `value` will be set to `null`.
- [API] Added `Date` class.  Example: `println "$ until Christmas" (Date.ymd(Date.now.year,12,25) - Date.now)`.
- [Rogue] Improved module namespacing.  When you write `module A::B`, all identifiers from modules `A` and `A::B` become visible (previously only identifiers from `A::B` become visible).  If you are working in `module A` that includes files from `module A::B`, you can now write relative scope qualifiers such as `B::xyz` instead of having to write the full `A::B::xyz`.
- [Rogue] Bug fix: only requisite singletons are instantiated on program launch.  Other singletons are instantiated on first access.
- [Time] Added `add(delta_time:Real64)` and `subtract(delta_time:Real64)` methods to classes `Stopwatch` and `Timer`.  Adding a positive value to a Stopwatch increases the `elapsed` time while adding a positive value to a Timer increases the `remaining` time.
- [Introspection] Added method template `TypeInfo.create_object<<X>>()->X` which is equivalent to calling `some_type.create_object as X`.
- [API] `Runtime.collect_garbage()` now sets a flag in manual GC mode to force a GC after the current update (the `force` flag is ignored in manual GC mode).
- [API] `List.remove(value:$DataType)->$DataType` now returns `value` instead of `null` if the value is not found in the list.
- [API] PropertyValue types now implement clone() and various operator methods.
- [API] PropertyValue types now have methods `ensure_list(key:String)->PropertyList` and `ensure_table(key:String)->PropertyTable`. When called on a PropertyTable they find or create the required collection and return it.  When called on any other PropertyValue type they return an empty collection of the appropriate type but do not (and cannot) store it.
- [API] Fixed JSON loading and parsing to treat EOLs as whitespace.
- [API] Improved JSON parsing so that a nested syntax error returns `null` at the top level instead of at the nested level.
- [API] Improved JSON parsing to not require commas after table mappings or list items.  Extra commas are ignored in tables and at the end of lists.  Extra commas within lists imply null elements - so `[1,,3]` parses as `[1,null,3]`.
- [API] `File.listing()` is now overloaded with a flag arguments variant that accepts `(&ignore_hidden,&recursive,&absolute_filepaths)`.
- [API] Added `File.ends_with_separator()->Logical` that returns `true` if a `/` or `\' is at the end of the filepath.
- [API] Added `File.ensure_ends_with_separator()`.
- [RogueC] Fixed `++` and `--` to work on objects that are accessed through a get/set interface - `++list[i]` becomes `list.set( i, list.get(i) + 1 )`.
- [RogueC] Fixed typed literal lists to work again (the newish visitor system did not yet support them).
- [RogueC] Improved logic that guards against recursive getters to prevent false positives - previously only the calling argument count was checked, now the callee parameter count is checked as well.
- [RogueC] If the directive `--output=folder/filename` is given, `folder` will automatically be created if it does not exist.
- [RogueC] Added `--compile` directive that compiles the RogueC output but does not execute it.  Automatically enables the `--main` option.
- [RogueC] Implicit narrowing reference casts are now illegal - the 'as' command must be used instead.
- [RogueC] Code generated for contingents now includes additional braces to scope local variables and prevent C++ warnings regarding jumping over local variable initializations.
- [RogueC] Dynamic method tables (vtables) are now generated for native classes (classes predefined in the Rogue runtime framework).
- [RogueC] `++` and `--` now call getters and setters if necessary for property adjustments.
- [Vim Syntax] Improved auto-indenting for verbatim strings (`@|Line 1\n  |Line 2\n  ...`) as well as routines.


###v1.0.59 - April 30, 2016
- [Rogue] Reworked introspection so that lists of properties and global properties are stored in TypeInfo objects instead of being obtained from instanced objects.  For example, `@Alpha.properties` and `@Alpha.global_properties` access corresponding `PropertyInfo[]` lists.
- [Rogue] Reworked introspection setters and getters to use runtime data rather than generating new methods Rogue-side.
- [Rogue] Global properties now have getters and setters via `TypeInfo` objects that behave like object introspection methods.  For instance, `@SomeClass.set_global_property<<Int32>>("x",3)`.
- [API] Shifted `collect_garbage()` and all introspection-related methods out of System class and into the Runtime class.
- [Rogue] Added new postfix unary operator `isReference` that returns true if its operand type is a class or aspect reference type and false if it is a compound or primitive value type.  Useful in type and method template definitions.  Example: `logical result = $DataType isReference`
- [Rogue] 'if' statement conditions that resolve to a literal `true` or `false` now clear the statement list of the opposing section, both as an optimization and as a way to write template code where `C++` would be unable to compile both sections of an type-based `if` statement.  Note that elseIfs are automatically converted to nested if/else blocks.  Also note that `if` statements in tasks may not receive this optimization.
- [Rogue] `PropertyInfo` objects now contain `property_name_index:Int32` and `property_type_index:Int32` as their core data and call `Runtime.literal_string(index:Int32)` and `Runtime.type_info(index:Int32)` to obtain property name strings and type info objects.

###v1.0.58 - April 27, 2016
- [RogueC] Fixed order-of-compilation bug relating to property types not being `organize()d` before property introspection methods were generated.
- [RogueC] Property introspection access methods now only handle properties defined in the current class and call the prior base class method to access any inherited properties.
- [Rogue] Each class's implicit `init_object()` method now calls `prior.init_object()` as its first step.  Inherited properties are now initialized by the prior method call and are not initialized in the extended class's `init_object()` method.

###v1.0.57 - April 26, 2016
- [Rogue] Changed property introspection setters to return `this` instead of nil.
- [Rogue] Compound property introspection setters now have the same signature as regular class setters.  However, the result must be reassigned to permanently change the compound.  For example, `local xy = XY(3,4); xy = xy.set_property<<Real64>>("y",6); println xy  # prints (3,6)`
- [RogueC] Compounds may incorporate aspects (as before) but now the compiler does not consider them polymorphically compatible with those aspect types.
- [RogueC] When a method specifies `this` as its return type, the method gains `Attribute.returns_this`.  References to `this` in aspects, including return values, are now converted into the incorporating type instead of remaining as the aspect type.

###v1.0.56 - April 25, 2016
- [Rogue] Added property introspection.  `obj.type_properties()->PropertyInfo[]` returns a list of property info objects.  For any given property info object one can access `info.property_name->String`, `info.property_type_name->String`, and `info.property_type_info->TypeInfo`.  To get and set class object properties through introspection, use `obj.get_property<<DataType>>(name:String)->DataType` and `obj.set_property<<DataType>>(name:String,new_value:DataType)`.  Due to the nature of compounds they use a different setter mechanism: write `compound_value = CompoundType.set_property<<PropertyType>>( compound_value, "property_name", new_property_value )`.
- [RogueC] Revamped method template system to be more robust and flexible, including: classes can now override specific versions of method templates as well as using specialized template syntax in method names even when no such template exists.  For instance, `method frobozz<<Int32>>(...)` overrides any inherited definition of template `method frobozz<<$DataType>>`.
- [API] Added an optional `allow_break_after:String` parameter to `String.word_wrapped()`.  If a line can't be broken on a newline or space it will be broken after any one of the supplied characters - the Rogue compiler specifies "," to break up long signature names.
- [RogueC] Improved formatting of error messages in several ways, including that "method not found" errors now strip package names from signature to avoid bloat.
- [Rogue] `trace` can now be used with formatted strings, e.g. `@trace "sum=$" (x+y)`.
- [Rogue] Any methods overriding `requisite` methods now inherit the `requisite` attribute.

###v1.0.55 - April 18, 2016
- [Rogue] Added method templates.  These operate as you might expect: method templates are defined with `method name<<$Type1,...>>(...)` and called with e.g. `obj.name<<Int32>>(...)`.
- [Rogue] Added `ensure` statement.  `ensure x` is convenience syntax for `if (not x) x = TypeOfX()`, `ensure y(a,b)` is equivalent to `if (not y) y = TypeOfY(a,b)`, and `ensure x && y(a,b)` performs both checks in consecutive order.
- [RogueC] Made named args more robust.  While the compiler used to wait until a method was selected before adding the named args back in, it now inserts them at the beginning of call resolution if it can infer their location (i.e. two overloads don't contain the same parameter name at different positions).
- [API] Made functional programming more flexible.  In addition to the in-place list modification methods `apply()`, `filter()`, `modify()`, and `sort()`, there are now versions of those methods that return a modified list instead: `applying()`, `filtered()`, `modified()`, and `sorted()`.  Note: `filtered()` is functionally equivalent to `choose()` and so the latter method has been removed.
- [API] Map/reduce functionality has been shifted from "helper classes" into List proper, made possible by method templates.  Their names are `mapped` and `reduced`, which is consistent with other functional programming methods in Rogue.
- [API] Added `List.remove(Function(T)->Logical)->List` that removes and returns the list of values passing the test function.
- [RogueC] Added method `Type.inject_method(Method)` that can be used to add a new method after the type has already been organized, correctly adjusting inherited method tables in extended classes.
- [RogueC] Creating an object as a standalone statement now omits the post-`init_object()` typecast which prevents a C++ warning.
- [RogueC] Renamed "flag args" to "named args" internally.

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

