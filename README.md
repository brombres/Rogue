Rogue
=====

About     | Current Release
----------|-----------------------
Version   | v1.2.4
Date      | May 13, 2017
Platforms | macOS, Linux (Ubuntu+), Cygwin



## Installation
    git clone https://github.com/AbePralle/Rogue.git
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

### v1.2.5 - May 20, 2017
- [enum] Added operator support for modulo (`%`).
- [List] Added utility method `List.expand(additional_count:Int32)->this` which adds additional null/0/false elements.

### v1.2.4 - May 13, 2017
- [enum] Comma-separated enum categories are now supported.
- [enum] Implicit typing now works with enumeration categories in more places - for example, a propery can be declared as `state = State.START` rather than requiring `state = State.START : State`.

### v1.2.3 - May 13, 2017
- [enum] All enums now have a `to->Logical` method that returns `true` when `value != 0`.
- [RogueC] An attempt to logicalize a compound, either explicitly with the `?` operator or implicitly by using it as an `if` condition etc., now requires that either method `Type.to->Logical` or global method `Type.operator?(Type)->Logical` be defined.

### v1.2.2 - May 9, 2017
- [Rogue] Added new `enum` types, described further here: https://github.com/AbePralle/Rogue/wiki/enum

### v1.2.1 - May 8, 2017
- [Rogue] Access commands now attempt to resolve to locals, properties, and then global properties, in that order.  This allows properties to shadow global properties and locals to shadow either.

### v1.2.0 - May 8, 2017
- [forEach] Changed the behavior of `forEach (... in/of <collection>)`.  When used with a collection that implements the `count`/`get` interface, Rogue now caches the collection count before the loop starts rather than polling the count every iteration, improving loop execution speed.
- [forEach] `forEach` now accepts a `from` clause after a random access collection.  For example, `forEach (n in list from 1)` and `forEach (n in list from 0..list.count-2)`.
- [GLOBAL PROPERTIES] Global properties are now visible to extended classes as one would expect.

### v1.1.38 - May 6, 2017
- [Dim] Added `Dim` utility class for creating fixed-size multidimensional lists.  E.g. `Dim<<Int32>>(3,4)` creates an `Int32[][]` of 3 by 4 dimensions, all initialized to the default `Int32` value of 0.  Specify 1 to 3 dimensions with 1 to 3 integer parameters.  An additional function parameter may be sent which accepts either zero argumemts or one integer index for each dimension and returns the element that should be place at the corresponding spot.  For example `Dim<<Int32>>(3,4,()=>-1)` returns a 3x4 2D list initialized with `-1` in all elements.  `Dim<<Int32>>(3,4,(i,j)=>i*10+j` returns a list initialized with element `[0]` containing `[0,1,2,3]` and element `[1]` containing `[10,11,12,13]`.

### v1.1.37 - May 4, 2017
- [RogueC] Fixed `global` vars to work correctly when declared without an initial value.
- [RogueC] Fixed `global` var assignment resolution bug.
- [RogueC] `global` vars can now only be declared in global scope outside of class definitions.

### v1.1.36 - April 30, 2017
- [Method Templates] Revamped method templates system.  Templates can now be overloaded and single type parameters can be inferred.  Fixed errors calling global method templates.  As before, method templates can be overridden by methods with generic and/or specialized type parameters - for instance, `method m<<$DataType>>` can be overridden with `method m<<$DataType>>` and by `method m<<Int32>>`.
- [Rogue] Changed augment section label syntax from `<<label_name>>` to `<label_name>`.
- [Rogue] Changed syntax for obtaining runtime type info by name from `@TypeName` to `<<TypeName>>`.
- [RogueC] Fixed bugs in global vs. object access checking.
- [Random] Added `Random.int64()` methods and a `Random.byte()` method.
- [Random] Replaced vestigial inline C with pure Rogue code.
- [List] Added two additional constructors `init(capacity,fn)` where `fn` is either `Function()->($Type)` or `Function(index:Int32)->($Type)`.  Example: `Byte[]( 16, ()=>Random.byte )`.
- [List] Made `List.init(capacity,initial_value)` `[preferred]` so that calling `List(n,null)` would not be ambiguous with `List(n,Function)`.
- [TypeInfo] Added `TypeInfo.instance_of(other:TypeInfo)->Logical`.  E.g. `obj.type_info.instance_of(@SomeClass)`.

### v1.1.35 - April 27, 2017
- [RogueC] Fixed a compiler crash that could happen with a certain combination of an array access in an internally cloned method.

### v1.1.34 - April 27, 2017
- [CompoundIntrospection] Added `.set_property(String,Value)` and `.get_property(String)` to make compound introspection symmetrical with Object introspection.  Compound introspection methods are injected into compounds automatically.
- [Object] Removed introspection method `Object.set_properties(Value)` (which works with a `ValueTable`) because when a developer-specific property named `properties` is declared the method inadvertently acts as a setter.  `Object.introspector.set(Value)` can be called instead to accomplish the same thing.

### v1.1.33 - April 25, 2017
- [RogueC] Improved handling of inline `native()` blocks in `[macro]` methods.
- [RogueC] Better error message when a class attempts to extend itself.

### v1.1.32 - April 16, 2017
- [Rogue] Changed syntax for implicit query functions.  Now, using `$` in an expression will cause the expression to be wrapped in a generic function `(value) => ...` and `$` changed to `value` (the generic function parameter).  For example, `list.discard($ is null)` is equivalent to writing `list.discard( (value)=>value is null )`.

### v1.1.31 - April 15, 2017
- [Implicit Queries] Fixed crash bug when `.this` is used in implicit query functions, e.g. `local nums = [3,4,5]; nums.discard(.this%2==1)`.

### v1.1.30 - April 11, 2017
- [Scanner/ParseReader] Renamed standard library class `ParseReader` to `Scanner`.

### v1.1.29 - April 10, 2017
- [RogueC] Added `use/escapeUse/endUse` control structure.
- [RogueC] Aspects `Reader`, `Writer`, and `PrintWriter` now implement the `use` API.
- [RogueC] Fixed call resolution issue when an aspect defines a method with the exact same signature as one of the methods in class `Object`.
- [RogueC] When an abstract method is called but no extended classes implement the method, a dummy method is generated to prevent a C++ compile error.  Previously the call to the method would be generated but the method itself would not be.

### v1.1.28 - April 5, 2017
- [Rogue] Changed root function types from aspects to classes and Task from a class to an aspect.
- [Rogue] Added inline native command `$(obj.type_name)` which prints out the C++ type name used for the given object.
- [Rogue] `global` variables now evaluate their initial expressions in the calling context instead of the `Global` class context.  For example, `local a = 3; global b = a` now works correctly.
- [Rogue] Added built-in define to map `IntPtr` to `Int64` or `Int32` as appropriate.
- [Rogue] Literal integers in bases 2, 8, and 16 (beginning with 0b, 0c, 0x) can now have underscores (`_`) immediately after `0x` etc. as well as later.  Underscores are visual separators only and do not change the value of the number.
- [Console] Revamped class `Console`.  Now write `Console.immediate_mode = true/false` to enter or leave immediate mode.  `Console.read()` now returns a Character instead of a Byte.  UTF-8 is automatically translated unless `Console.decode_bytes = false`.  Likewise arrow keys are automatically translated into Unicode 17-20, `Console.UP_ARROW` etc.

### v1.1.27 - April 1, 2017
- [Cython] Fixed Functions in Cython.  `c42d4d0` changed Functions from classes to aspects, which required changes
to the Function thunking for Python bindings.

### v1.1.26 - April 1, 2017
- [RogueC] `--essential` flag no longer causes errors in types that extend a `Function` type that specifies a `$PlaceholderType` return type.
- [Random] Removed deprecated methods.

### v1.1.25 - March 30, 2017
- [RogueC] Compiler now reports error messages with better line locations for `DEFINITIONS` and `[macro]` methods.
- [C++ Runtime] Expanded debug stack trace print buffer from 120 to 512 characters and now using snprintf() instead of sprintf().


### v1.1.24 - February 23, 2017
- [Int32] Added `to->Int64(&unsigned)`.
- [DataReader] Fixed sign extention error in `DataReader.read_Int64()` and `read_Int64X()`.
- [Character] Added `to_uppercase->Character` and `to_lowercase->Character`.
- [FileWriter] `close()` calls `System.sync_storage()`, which is a `noAction` in Rogue but can be augmented as needed in Plasmacore etc.

### v1.1.23 - February 1, 2017
- [Random] Deprecated `next_int32` and renamed to `int32` etc.
- [Rogue] Added `[deprecated]` attribute and `deprected ["Message"]` statement (the latter implying the former) that causes Rogue to generate compile warnings if a deprecated type or method is used.

### v1.1.22 - January 30, 2017
- [RogueC] A generic function with no parameters and a `with` clause is now parsed correctly (`() with(x) => ...`).

### v1.1.21 - January 22, 2017
- [Rogue] `ensure <var>` can now be used as an expression.  Example: `if (not list) list = Int32[](5); list.add( n )` becomes `(ensure list(5)).add( n )`.
- [Rogue] Renamed `[nonessential]` to `[nonAPI]`.
- [Rogue] Operand parens are now optional on `swapValues a,b`
- [RogueC] Fixed `[api]` attribute to work on classes that are totally unreferenced by the rest of the program, making it and all of its `[nonAPI]` methods [essential]`.

### v1.1.20 - January 19, 2017
- [RogueC] Fixed compiled switch-case fall-through error when calling aspect methods that don't return values.
- [RogueC] Decoupled the introspection framework from the core CPP code, allowing it to be fully culled out when unused.
- [Rogue] Added `[nonessential]` method attribute that prevents a method from being automatically considered `[essential]` when the context type has the `[api]` attribute.
- [Cython] Compiling `SomeFile.rogue` using `--target=Cython` now generates the following files: `somefile.pyx`, `somefile_module.h`, `somefile_module.cpp`.  The `.pyx` file should by Cython-compiled to `somefile.cpp` and then all .cpp files should be compiled and bundled as `somefile.so`.  In Python the module can then be imported with `import somefile`.
- [Cython] Tweaks - added a placeholder 'pass', removed call to type Exception `make_api()`.
- [Character] Added `Character.is_uppercase()->Logical` and `Character.is_lowercase()->Logical`.  Only works for ASCII.

### v1.1.19 - January 17, 2017
- [Rogue] Function types are now aspects rather than class types, meaning any class with an appropriate call() method can also incorporate the appropriate Function type.  For example, class X with a method `call(Int32)->Logical` can now be declared as `class X : Base, Function(Int32)->(Logical)` and objects of type X can be passed using `Function(Int32)->(Logical)` parameters.
- [Rogue] Instantiated tasks can now be called like methods.  Nil-return tasks will continue to the next `yield` and value-return tasks will continue to the next `yield <value>` or `return <value>`.  Ideal for generators.  If `Fibonacci()` is a `[task]` routine, then call `local fib = Fibonacci()` to instantiate the task and then `fib()` to obtain the first number in the sequence.
- [RogueC] The operator overloading mechanism now searches type Object if an original operand type is an aspect.  For example, "aspect1 == aspect2" no longer fails to compile because because it finds `Object.operator==(other:Object)->Logical` as a suitable method.
- [RogueC] Better error message when a function return type is missing its (parens).

### v1.1.18 - January 16, 2017
- [RogueC] Reworked CmdStatementList evaluation mechanism to fix literal lists and other commands which add new nodes to the scope's statement list during their resolve phase.
- [Rogue] Added new macro statement `swapValues(a,b)` which expands to: `local <temp> = a; a = b; b = <temp>`.
- [Timer] Fixed bug in `Timer(duration,&expired)` when `&expired` flag is true.  Timer was starting out in a stopped state and also reading as non-expired due to roundoff error.  Now timer does not begin in a stopped state AND the start time is adjusted as necessary to ensure that `is_expired()` returns true.

### v1.1.17 - January 10, 2017
- [Rogue] Rogue now supports the `yield <value>` command in tasks.  `yield` surrenders execution without returning a value.  `yield <value>` surrenders execution and returns a value.  The command `await <task_name>` waits for a `yield <value>` and is not satisfied by a simple `yield`.
- [task] Routines and global methods can now be made into tasks with the `[task]` attribute.
- [Wiki] Added Task system documentation to Wiki: <https://github.com/AbePralle/Rogue/wiki/Tasks>

### v1.1.16 - January 8, 2017
- [ParseReader] Constructors now accept optional flag `&preserve_crlf`.  Unless the flag is set to true then CR characters (Unicode 13) will be stripped.
- [RogueC] Fixed to work with files containing CRLF (CR characters are stripped).
- [RogueC] Fixed compiler error caused by a pending syntax error when '--' is misused inside functions and possibly other places - CmdAdjust.cloned() was not implemented and the base Visitor class did not accept a generic CmdAdjust.

### v1.1.15 - December 29, 2016
- [RogueC] Compiler no longer crashes on property declarations initialized to list values, including implicitly and explicitly typed lists as well as ValueList types.
- [RogueC] Reworked literal ValueTable construction mechanism to be more robust - previously a large number of definitions would exceed the C++ call nesting limit.
- [Rogue] Added meta-variable `$methodSignature`.
- [UnsupportedOperationError] Constructing an `UnsupportedOperationError()` now prints an error message identifying the caller's class context and method signature.
- [JSONParser] Turned global property `JSONParser.buffer` into singleton `JSONParserBuffer` to avoid order of class initialization issues.
- [Stopwatch] Added constructor `Stopwatch(&stopped)`.  The Stopwatch will automatically start counting (as before) unless the `stopped` flag is sent.
- [Timer] Added constructor `Timer(duration,&expired)`.  If the `expired` flag is set then the timer starts out stopped and `is_expired`.  Calling `restart` on it will use the duration paramater.

### v1.1.14 - December 15, 2016
- [RogueC] If `C++` and `ObjC` are both listed as compile targets then the output files are `.h` and `.mm` instead of `.h` and `.cpp`.
- [RogueC] Added compile flag `--ide[=<IDE Name>` that prints errors in classic C style for IDE's to pick up.  Realized previous approach of using a `--define` wasn't conceptually sound because the define targets the program being compiled rather than the compiler itself.
- [System] `System.os()` now returns `"emscripten"` if appropriate.

### v1.1.13 - December 12, 2016
- [Rogue] Moved location of compiled `roguec` executable from `Programs/RogueC/roguec` to `Programs/RogueC/<Platform>/roguec`.  The Makefile will automatically delete the old program and `bin/` link.
- [Rogue] Added `$defined(<identifier>)` directive that returns true if a compiler option `--define=<identifier>[:<value>]` was given or if a prior `$define <identifier> <tokens>` directive was issued.
- [Rogue] Added metavariables `$sourceFilepath` and `$sourceLine` that can be used in expressions.
- [Rogue] Now prints out "classic" style error messages if `$defined(IDE)` or Rogue-style error messages if not.  Classic error messages automatically show up in XCode and possibly other IDEs.
- [System] Added `System.os()->String` that returns the one of: `macOS`, `iOS`, `Linux`, `Windows`, `Android`.
- [File] Added `File.is_newer_than(other_filepath:String)->Logical` that returns true if the other file does not exist or if this file is newer.  As usual with `File` methods the global method `File.is_newer_than(this_filepath,other_filepath)` may also be called.
- [File] Added `File.line_reader()->LineReader`.
- [Reader] Added `close()->this` to `Reader<<$DataType>>` and appropriate forwarding calls in `LineReader` and `UTF8Reader`.
- [String] Added `String` methods `is_number()->Logical`, `is_integer()->Logical`, and `is_real()->Logical`.
- [RogueC] RogueC now detects recursive macro definitions and throws an error instead of crashing.
- [Makefile] Fixed initial roguec build to print out instructions for creating `Hello.rogue` instead of actually creating that file.
- [Syntax] Updated Vim Syntax file to include all directives and metavariables.  Did not add to Sublime; not sure on the syntax to include a leading `$`.
- [C++] The generated .h file now identifies `ROGUE_PLATFORM_IOS` or `ROGUE_PLATFORM_MACOS` if appropriate.

### v1.1.12 - December 8, 2016
- [Rogue] Literal numbers can now contain underscores as visual markers so long as the number does not start with an underscore.  They do not change the value of the number and are intended to be analogous to commas - for example, one million can be written as `1_000_000`.
- [Rogue] Added new statement `compileError ["optional message"]` that generates a compile error if and when it is parsed.  Useful for informing the developer that a certain API is not implemented for their chosen target.
- [System] Added `System.sleep(Real64)` that uses `nanosleep()` with nanosecond (1/1,000,000,000 second) precision.
- [Sockets] Tweaked Socket and ServerSocket class hierarchy in preparation for SecureSocket and SecureServerSocket.
- [RogueC] Fixed order-of-class-declaration bug where an overridden method's covariant return type would not always be accepted.
- [RogueC] An unused singleton access (useful to initialze a non-`[essential]` singleton without accessing a specific property or method) now omits the typecast on the result in C++ to avoid an "unused value" error.
- [Vim] Updated Vim and Sublime syntax files to support numbers with underscores and the new `compileError` statement.

### v1.1.11 - December 3, 2016
- [RogueC] Detailed Rogue error messages now show up in Xcode and can be jumped to by clicking on the error.
- [RogueC] Any error now prints out in two forms: first the "classic" compiler error format of `filepath:<line>: error <message>` to `stderr` followed by the Rogue standard error format that is more easily readable printed to `stdout`.  Xcode (and perhaps other IDE's) automatically parse and display errors printed in that classic format.
- [RogueC] Added `Console.error:PrintWriter`; printing to it prints to `stderr` instead of `stdout`.

### v1.1.10 - November 30, 2016
- [RogueC] Fixed up compiler warnings that appear on Cygwin and Linux but not Mac.

### v1.1.9 - November 30, 2016
- [RogueC] `roguec` `--execute` and `--compile` options now use the Rogue Makefile default $(CXX) compiler with certain other flags - see `DEFAULT_CXX` in Makefile.
- [RogueC] `roguec` `--compile=<compiler invocation>` can be used to override the default.

### v1.1.8 - November 30, 2016
- [RogueC] `roguec` now compiles, installs, and works correctly on Mac, Ubuntu, and Cygwin.
- [RogueC] Fixed `roguec` compile on Ubuntu by specifying `-std=gnu++11`, which works on Mac, Ubuntu, and Cygwin.
- [RogueC] Now suppressing "offsetof macro used incorrectly" message which shows up in Ubuntu.

### v1.1.7 - November 30, 2016
- [Sockets] Added `Socket(address:String,port:Int32)` and `ServerSocket(port:Int32)` in `Libraries/Standard/Sockets.rogue`.  Sockets are non-blocking and poll-driven.  Once a `Socket.is_connected()` or a non-null Socket is obtained from `ServerSocket.accept_connection()->Socket`, use the Socket `reader()->Reader<<Byte>>`, `character_reader()->Reader<<Character>>`, `writer()->Writer<<Byte>>`, and `printer()->PrintWriter` I/O objects to communicate via the socket.
- [String] Added `String.to->Int64`.
- [Runtime] Fixed small object allocations to be probably 16 byte-aligned - data array now starts from offset 0 of new allocation rather than offset 20 as before.
- [RogueC] Changed Makefile to use `-std=gnu++14` instead of `-std=c++11` to fix a Cygwin issue with sigemptyset.
- [RogueC] Changed `roguec` `--execute` option to use `g++` instead of `clang++` and changed `-std` to use `gnu++14`.
- [RogueC] Removed reference to UNLICENSE in embedded Rogue runtime (`NativeCPP.cpp`).

### v1.1.6 - November 22, 2016
- [RogueC] Restored `-include Local.mk`.

### v1.1.5 - November 21, 2016
- [String] Added explicit `Int32` cast to native implementation of `String.to->Int32`.
- [RogueC] Reworked implicitly and explicitly typed list construction to avoid exceeding max nesting level in C++.

### v1.1.4 - November 15, 2016
- [Stopwatch] Added `Stopwatch.stop()->this` and `Stopwatch.start()->this`.
- [Timer] Added `Timer.stop()->this` and `Timer.start()->this`.
- [Console] Added `Console.read_line(prompt:String)->String` and `Console.read_line(prompt:StringBuilder)->String` in addition to existing `Console.read_line()->String`.

### v1.1.3 - November 13, 2016
- [String] Added optional `max_count` argument to `String.leftmost_common_substring_count(other,[max_count=null:Int32?])->Int32`.
- [Value] Added `Value.compressed()->Value`.  Returns a clone of the value where all identifiers have been replaced by indices into an ID table.
- [Value] Added `Value.decompressed()->Value` as a corollary to `compressed()`.  `decompressed()` may be safely called on an uncompressed value as long as it is not a table that contains elements `@id_list` and `@indexed_data`.
- [Value] Changed behavior of `Value.locate(query:Function(Value)->(Logical))->Value` to return the index or key of the first matching item rather than the indices of all matching items.
- [Value] Added `Value.locate_last(Value)->Value` and `Value.locate_last(query)->Value`.
- [Value] Fixed bugs in `ValueList.to_json()` and `ValueTable.to_json()` that were converting `false` values to `null`.
- [List] Changed `List.to->Value` to convert non-null elements to values with `element->Value` rather than `Value(element)` - the latter results in values wrapping regular objects while the former converts the objects themselves to values.
- [Table] Changed `Table.to->Value` to convert non-null elements to values with `element->Value` rather than `Value(element)`.
- [RogueC] Literal value lists (`@[ ... ]`) now use a different construction mechanism to prevent a "too many nested elements" error in C++ when dealing with large lists.

### v1.1.2 - November 2, 2016
- [RogueC] Changed default gc mode to `auto`.  Pass `--gc=manual` to `roguec` to specify the previous default.
- [Rogue]  Renamed `clean_up()` to `on_cleanup()`.  This method can be present in any object and, if it is, will automatically be called when the object goes out of scope.
- [Task] `Task.stop()` can now be called to stop any task and remove it from the `TaskManager`.
- [List] List methods `random()`, `shuffle()`, and `shuffled()` now all accept an optional `Random` generator argument.  If no generator is passed then the `Random` singleton is used as before.

### v1.1.1 - October 29, 2016
- [File] Improved wildcard behavior, e.g. `File.listing("A/*/*.txt")` will list out files ending with `.txt` in folders `A/B`, `A/C`, etc.
- [File] `File.listing(folder,...)` can now specify a specific non-folder file as its "folder" argument and that filepath will be returned in the results list.  For example, `File.listing("text.txt")` now returns the `String[]` list `["test.txt"]` if the file exists.
- [File] Renamed `FileOptions.omitting_paths()` to `.omitting_path()` to match singular name of related methods and variables.

### v1.1.0 - October 20, 2016
- [Rogue] When `$include`ing a file, RogueC now first tries to find the file in the same folder as the including file, making filename collisions across separate libraries much less likely.  Previously RogueC would just iterate through a list of all folders that had been seen so far, trying each one with no regard to the folder of the including file.
- [String[]] Renamed `String[].joined(separator:String)->String` to `String[].join(...)`.  While the `-ed` adjective style indicates a non-mutating call, is is also generally reserved for methods that return the same data type as the context.  Because `join()` does not return `String[]` it is better left as a verb.
- [String] Renamed `String.word_wrapped()->String[]` to `String.word_wrap()->String[]` for the same reasons given in the previous bullet point.
- [String] Added new `String.word_wrapped(...)->String` that returns a string with embedded `\n` characters rather than returning a list of strings.
- [String] All word-wrapping methods now preserve any initial indentation on successive lines.
- [String] Fixed bug in `String[].joined()`: if the first list item was an empty string then the separator wasn't being added after it.  This was due to faulty logic that used a non-empty result string as an indicator that the loop was past its first iteration.
- [String] Added `after_prefix(Character/String)->String` and `before_suffix(Character/String)->String`.
- [String/File] Moved `String.matches_wildcard_pattern()` to class `File` and altered wildcard behavior: `**` matches any sequence of characters, `*` matches any characters except `/` or `\`, and `?` matches any single character except '/' or '\'.
- [File] Added `File.folder(...)` that is equivalent to `File.path(...)`; the older method still exists (continuing terminology shift of "path"/"directory" to "folder").
- [File] Added `File.before_wildcard_pattern()->String`.
- [File] Added `File.copy(to_filepath:String)` and `File.copy(from_filepath:String,to_filepath:String)`.
- [File] `File.listing(...)` can now handle wildcard patterns - `**` implies recursive, `*` matches any character except slash, and `?` matches any single character.
- [JSON/Value] Added `&omit_commas` flag parameter to `save()` and `to_json()`.  Passing it automatically sets the `&formatted` flag as well.  Rogue is capable of reading the non-standard JSON that is produced with `&omit_commas`; the format is useful for allowing concurrent changes to the same JSON data file to be merged with less chance of a conflict.
- [Value] Added `Value.set(Value,Value)` which forwards to `Value.set(String,Value)`.
- [Value] Fixed `Value.to_json()` call to work with no arguments - problem was that two overloads would both accept zero arguments.
- [Console] Added `Console.width()->Int32` and `Console.height()->Int32` that return the size of the console in characters.
- [PrintWriter<<...>>>] Calling `flush()` on a PrintWriter aspect template now invokes the PrintWriter's `write()` method even if the StringBuilder buffer is empty.
- [Set] Added `get(Int32)->$T` so that array-style access works.
- [Set] `discard(value:$T)` and `remove(value:$Type)` now return the value being removed whether or not it exists in the set (previously no value was returned).  This allows the value to be removed to be computed from an expression, removed, and stored in a local all in one step.
- [Primitives] Added `sqrt()` to all numerical primitives.
- [ParseReader] All init() methods now reset 'position' to 0, allowing a `ParseReader` object to be reinitialized multiple times.
- [RogueC] Top and bottom bars around error messages (`======`) now scale with the width of the console up to 79 characters max.
- [Syntax] Added keyword `downTo` to syntax highlighting files.


### v1.0.102 - September 27, 2016
- [Rogue] Removed ability to write `method set-x` as a shorthand for `method set_x` - was just not feeling natural enough.
- [RogueC] Fixed some new Xcode weirdness around `std::set_terminate()` by removing `std::` and adding `namespace std{} / using namespace std;` kludge in the generated code... more conventional approaches were failing either on the command line or in Xcode (take out `std::`, add `using namespace std;`, include `<cstdio>`).


### v1.0.101 - September 24, 2016
- [API] Changed `File.listing(path:String,[flags])` behavior:
    - As before, sending the `&absolute` flag returns results that include the absolute path.
    - Sending the `&omit_path` flag returns results that omit the original path sent to `listing()`.  Previously this was the default behavior.
    - When neither of the above flags is specified, `listing()` uses "relative" behavior where the results include the original path as well as any folders encountered during `&recursive` descent.

### v1.0.100 - September 16, 2016
- [API] Added `Runtime.gc_logging:Logical` pseudoproperty.  Defaults to `false`, enable to see used object and byte count after a GC.
- [API] Added `Runtime.memory_used()->Int32` that returns the number of bytes currently used by Rogue objects.
- [API] Added `Runtime.object_count()->Int32` that returns the Rogue objects that currently exist.
- [API] Fixed two List method attributes from the out-of-date `[nullArgDefault]` to the current `[preferred]`.
- [API] Fixed error in `TimeInterval` (`Date.rogue`) `[macro]` methods - the term `total_seconds` should have been `this.total_seconds` in all cases.

### v1.0.99 - September 10, 2016
- [API] Added `.is_power_of_two()->Logical` to all numerical primitives.
- [API] Added `.to_power_of_two()` to `Character` and `Byte` types (already exists in all other numerical primitives).

### v1.0.98 - September 7, 2016
- [Rogue] Renamed `[nullArgDefault]` method attribute to `[preferred]` and broadened its effect.  Now, instead of pertaining only to `null` arguments, a `[preferred]` method is chosen over other candidates any time that an ambiguous call is about to generate a compile error.  If an ambiguous call has multiple preferred methods, none of them are chosen and an ambiguous call error is generated.
- [API] Added `Value.insert(value:Value,before_index=0:Int32)` and `Value.insert_all(value:Value,before_index=0:Int32)` for inserting additional values into lists.
- [API] Added `JSON.load_list(File)->Value` and `JSON.load_table(File)->Value` that return empty list or empty table if there are any problems loading or parsing the file.

### v1.0.97 - September 4, 2016
- [API] Fixed issue with `List.get(Int32)`.  It had been changed from a macro method to a regular method while debugging during the development of v1.0.91 and was inadvertently not changed back into a macro method.  This prevented compound properties from being set directly on a list element since without the macro in place they were being set on a returned compound value rather than on a derferenced compound pointer value.

### v1.0.96 - August 31, 2016
- [Rogue] `Function()` Type declarations and `function()` object declarations now require parens around the return type to solve ambiguity issues.  For instance, existing code containing `Function()->Logical` must now be written `Function()->(Logical)`.
- [Rogue] Added method attribute `[nullArgDefault]`.  When specified, an ambiguous call with a `null` parameter with multiple candidate methods will now select the method that specifies `[nullArgDefault]`.  For instance, `List.add($DataType)` is now the `[nullArgDefault]` vs. `List.add($DataType[])` and so calling `list.add(null)` will call the first overload.
- [API] Added `[nullArgDefault]` to `List.add($DataType)` and `List.locate($DataType)`.
- [API] Added `PrintWriter.close()` that forwards the call to the an writer if it exists.

### v1.0.95 - August 30, 2016
- [API] Added `Byte[](hex:String)` and `Byte[].add(hex:String)->this` that accept strings of hexadecimal pairs and convert them into bytes.  For example, `Byte[]("007fa0FF")` produces a Byte list equivalent to `Byte[][0,127,160,255]`.
- [API] Added `Byte[].to_hex_string()->String` that returns a string of hexadecimal digit pairs encoding each byte.  For example, `Byte[][0,127,160,255].to_hex_string` returns `"007FA0FF"`.
- [API] Added `Int64/Int32/Byte.to_hex_character()->Character` that returns characters `0..9` or `A..Z` for values `0..9` or `10..35` ("hex" isn't entirely accurate as this method naturally supports bases 2..36, but hexadecimal is the most likely use case).
- [RogueC] Literal `Byte[]` lists containing only literal byte values are converted internally to use an efficient `Byte[](hex:String)` constructor rather than generating an `add()` statement for each byte value.
- [RogueC] Extended types can now be implicitly cast to optional base types.  For example, you can return a `Cat` from a method that returns `->Pet?`.
- [RogueC] Fixed ambiguity issue when a Function Type is used as a template type parameter as an optional or list value.  In other words, if `$DataType` is `Function()->Int32` in a method `method m->$DataType?`, the return type is now correctly interpreted as `->(Function()->Int32)?` rather than `->(Function()->Int32?)`.
- [RogueC] Renamed various `Cmd.cloned(Cmd)` methods to `Cmd.clone(Cmd)`, leaving `Cmd.cloned()` as-is, to have the semantics reflect the behavior of possibly cloning a possibly null reference (`clone()`) versus having an existing object clone itself (`cloned()`).
- [RogueC] Fixed bug where `CmdCallPrior` nodes were assuming that arguments always existed during cloning.  Changed `args.cloned()` to `clone(args)`.

### v1.0.94 - August 26, 2016
- [RogueC] Changed comma from a standard expression token to a "structural" token.  This allows generic functions to be parsed as a middle argument (followed by a comma) rather than inadvertently restricting them to be the final argument as was previously the case (close paren is also a structural token, which stops the parsing in that scenario).  There are no known negative side effects of this change.

### v1.0.93 - August 25, 2016
- [Rogue] All occurrences of the pattern `set-<x>` are now replaced with `set_<x>`.  For instance, `set-value` is shorthand for writing `set_value`.  This is a more complete implementation of an existing mechanism; the purpose is to easily allow pattern searching to find properties, getters, and setters all using the same name (e.g. searching for `\<value\>` in Vim finds property `value`, getter `value()`, and setter `set-value()`).

### v1.0.92 - August 23, 2016
- [RogueC] Parsing of implicit `forEach` loops is now more robust.
- [JSON API] JSON parser now returns `UndefinedValue` rather than `NullValue` in syntax error situations.  `NullValue` is only returned when the literal keyword `null` appears in the JSON.
- [Value API] Added `Value.is_undefined()->Logical` that returns `true` when a value is an `UndefinedValue`.  Note that `UndefinedValue` also returns true for `.is_null()`; an `UndefinedValue` is a `NullValue` but the reverse is not true.

### v1.0.91 - August 19, 2016
- [Rogue] Generic functions can now omit the keyword 'function' for brevity and begin with `(args)=>`, `()=>`, or even just `=>`.  For example: `trace [3,1,5,4,2].sort( (a,b)=>(a<b) )`.
- [Rogue] Added an *implicit function* convenience syntax that automatically converts expressions into generic single-parameter functions with automatic variable capture when the expression contains terms that begin with `.` or `//`.  For example, `table[//name==player_name]` is equivalent to writing `table[function(value) with(player_name)=>value//name==player_name]` and `list.first(.is_string and .count>3)` is equivalent to writing `list.first(function(value)=>value.is_string and value.count>3)`.  The special keyword `.this` may be used in implicit functions to indicate the value under consideration.  For example, to pull out all the odd numbers in an Int32 list: `list[ .this & 1 ]`.  Methods accepting functions of this form can be called *function methods*.
- [Rogue] Added *implicit loops*.  Any expression `(forEach in/of collection)` is replaced by a variable and the statement that contains the expression is wrapped in a `forEach (<variable> in/of collection)` control structure.  For example, writing `(forEach in sprites).update` is equivalent to writing `forEach (sprite in sprites) sprite.update` and `println (forEach in nums)` is equivalent to `forEach (num in nums) println num`.
- [Rogue] Added an additional refinement to call resolution: if a call would be otherwise ambiguous and any `Value` arguments or parameters exist, keep only candidate methods where there is at least one `Value` type in each argument/parameter pairing.  In other words, a call `m(5)` would match `m(Value)` and not `m(OtherType)`.
- [Rogue] Explicit calls to operator methods (`a.operator==(b)` etc.) can now be made.
- [Rogue] Implemented *compare operator* `<>`.  An expression `a <> b` resolves to `-1` if `a < b`, `0` if `a == b`, and `1` if `a > b`. Classes may implement the *compare operator* by defining global methods `method operator<>(TypeA,TypeB)->Int32` and/or instance methods `method operator<>(OtherType)->Int32`.
- [Rogue] Reworked the operator methods mechanism.  For any general binary operator such as `+` where at least one operand is not a primitive, the expression `a + b` (of types `TypeA` and `TypeB`) is converted to the first of the following expressions that are implemented:
    1.  Global method `TypeA.operator+( a:TypeA, b:TypeB )`.
    2.  Global method `TypeB.operator+( a:TypeA, b:TypeB )`.
    3.  Instance method `TypeA.operator+( b:TypeB )`.
- [Rogue] 'local' variable declarations are no longer allowed in single-line statement blocks (part of the fix for local variables in multi-line 'function' definitions described below).
- [Rogue] Added parse support for the escape sequence `\e` (ASCII 27 / Escape).
- [API] `Table` has been completely revamped to track the order of its values by implementing a doubly-linked ordering list as part of the `TableEntry` class, making removals O(1) instead of O(n) as before.
- [API] Renamed `Table._remove(TableEntry)` to `Table.remove(TableEntry)`.
- [API] `Table` now uses an array of bins instead of a list of bins.
- [API] `Table` automatically doubles in size every time its item count matches its bin count.
- [API] Added `Table.entries(list=null:TableEntry[])->TableEntry[]` that returns a list of table entries.
- [API] Added optional list parameter to `Table.keys()` and `Table.values()` which, if it exists, is used instead of dynamically allocating a list of keys or values.
- [API] Added `Table.sort(Function(TableEntry,TableEntry)->Logical)->this` and `Table.sorted(Fn...)->Table`.  The former sorts the curent table in place and the latter returns a sorted clone.  Example: `people.sort( (a,b)=>(a.value.age < b.value.age) )`.
- [API] Added optional `Table.sort_function : Function(TableEntry,TableEntry)->Logical` that, if defined, is used to sort table entries as they're defined or redefined.  Setting the sort function will automatically sort() any existing table entries using that function.
- [API] Fixed error in `Table.remove(key)` - correct entry was being removed but incorrect value was being returned.
- [API] Renamed the function method `Table.keys(Function(...))` to `Table.locate(Function(...))`.
- [API] Added several *function methods* to `List` and `Table`: `.get(query)->$DataType[]`, `.first(query)->$DataType?`, `.locate(query)->$DataType[]/Int32[]`, `.remove(query)->$DataType[]`, and `.count(query)->Int32` (the latter has been added to `Value` as well).`
- [API] Added `.contains(query)` to `List`, `Table`, and `Value`.
- [API] Added `List.rest(result_list=null:List)->List` that returns all elements after the first.
- [API] Added `Array<<DataType>>.cloned()->Array<<DataType>>`.
- [API] `Value.cloned()->Value` now performs a deep clone for `ValueList` and `ValueTable`.
- [API] Fixed several `Value` operator methods to perform the correct operation instead of addition (copy/paste error).
- [API] Changed `Value.operator?(Value)` to report true for any non-null, non-LogicalValue, or true LogicalValue (and false for any null or Logical false).  Similarly changed ValueList and ValueTable `to->Logical` to always return true.
- [API] Fixed `Value.remove(String)` for `ValueList` (the nominal purpose of remove(String) is to remove table values by key but of course it needs to work on lists of strings as well) and added some better default implementations for `Value` methods `.first()`, `.last()`, `.remove_first()`, and `.remove_last()`.
- [API] `Value.sort(compare_fn)->Value` now works on ValueTable as well as ValueList (`compare_fn` receives table values only, not entries or keys).
- [API] Renamed `values` property of `ValueList` and `ValueTable` to `data`.
- [API] Added `Value.values(list=null:Value)->Value`.  For tables, returns a ValueList of the table data.  For anything else (including ValueList) returns the value itself.  If a ValueList parameter is provided, values will be added to the list and it will be returned instead of creating and returning a new list.
- [API] Added `Value.add_all(Value)->this` which adds each item in the parameter to this list if the parameter is a collection or adds just the parameter itself if the parameter is not a collection.
- [API] Added `Value.rest(result_list=null:Value)->Value` that returns all the elements in a `Value` collection after the first.
- [API] Added `Value.reserve(additional_elements:Int32)->this`.  If the `Value` is a `ValueList` the call is forwarded to the backing list; otherwise there is no effect.
- [API] Added global methods `Value.load(File)->Value` and `Value.parse(json:String)->Value`.
- [API] Added method `Value.save(File,&formatted)->Logical`.
- [API] Reworked `String` operator methods to prevent null pointer errors.
- [API] Added `String.leftmost_common_substring_count(other:String)->Int32` and `String.rightmost_common_substring_count(other:String)->Int32`
- [API] Added convenience method `Console.clear`.
- [API] Added convenience method `Console.clear_to_eol`.
- [API] Added convenience method `Console.move_cursor(dx:Int32,dy:Int32)`.
- [API] Added convenience method `Console.restore_cursor_position`.
- [API] Added convenience method `Console.save_cursor_position`.
- [API] Added convenience method `Console.set_cursor(x:Int32,y:Int32)`.
- [API] Added global method `File.delete(filepath:String)->Logical` and method `File.delete()->Logical`.
- [RogueC] Reworked and simplified code handling resolution of `prior.init` calls to fix a new `--exhaustive` compile error that chose now to crop up.
- [RogueC] Fixed local variables to work correctly in function definitions.  Multi-line function bodies were being parsed at the same time as function declarations, meaning that the `Parser.this_method` reference while parsing a local was either null or incorrect.  Multiline function definitions now simply collect tokens and wait for the resolve() phase to parse the function bodies.

### v1.0.90 - August 6, 2016
- [Rogue]  Renamed `[requisite]` to `[essential]`.
- [RogueC] Added Python plug-in functionality from Murphy McCauley with `Cython` compile target.
- [RogueC] Added `--exhaustive` option which makes all classes and methods `requisite`.
- [RogueC] Added `--api` option which adds the `[api]` attribute to every class - if the class is used then all methods are resolved generated even if unused.
- [RogueC] Fixed logicalize (`?`) to search for a *global method* rather than object method `operator?(operand)->Logical`.  `Type.has_method_named(String)` used to check both object and global methods but for some time now it only checks the former and `Type.has_global_method_named(String)` must be used to check the latter.
- [API] Added `Value.get(Function(Value)->Logical)->Value`.  Example: `local teens = people[function(p)=>(p//age>=13 and p//age<=19)]`.
- [API] Added `Value.remove(Function(Value)->Logical)->Value`.  Example: `local teens = people.remove( function(p)=>(p//age>=13 and p//age<=19) )`.
- [API] Added `Value.keys(Function(Value)->Logical)->String[]`.  Example: `local teen_ids = people.keys( function(p)=>(p//age>=13 and p//age<=19) )`.
- [API] Added `Value.first(Function(Value)->Logical)->Value`.  Example: `local first_teen = people.first( function(p)=>(p//age>=13 and p//age<=19) )`.
- [API] Added `Value.last(Function(Value)->Logical)->Value`.  Example: `local last_teen = people.last( function(p)=>(p//age>=13 and p//age<=19) )`.
- [API] Console I/O now goes through `Console.io_handler`.  In addition to the default `BlockingConsoleIOHandler` there is also an `ImmediateConsoleIOHandler` you can instantiate and install - or you can extend your own `ConsoleIOHandler`.
- [API] Renamed the Value system's `NullResult` to `UndefinedValue`.
- [API] Added `Math.min/max(Int64)`.
- [API] Added `File.append_writer()`.
- [API] `ConsoleIO.write(String)` bugfix.
- [API] `List.locate(null)` is now handled properly
- [API] `Error.format()->String` is now more cautious.
- [Syntax] Fixed `skipIteration` (a briefly considered alternative) to be `nextIteration` in the Vim and Sublime syntax files.
- [Syntax] Renamed `requisite` to `essential`.

### v1.0.89 - August 2, 2016
- [API] For GC safety, introspectors now retain references to objects they were created from (instead of only having the raw memory address).
- [API] Value types now have two kinds of null Value.  `NullValue` is what is stored when a `null` is stored, retrieved, or parsed and its `to->String` is `"null"`.  `NullResult` extends `NullValue`, is what is returned when an element cannot be found, and its `to->String` is `""`.


### v1.0.88 - August 1, 2016
- [API]  A new class "Introspector" provides the ability to get and set object value using introspection and JSON-style `Value` types.
    - Obtain an introspector from any by-reference or by-value type by accessing `.introspector` on the value.
    - `Introspector.get(name:String)->Value` returns the `Value` representation of the specified property.
    - `Introspector.set(name:String,new_value:Value)->this` sets the specified property, if found, to be the new value.
    - `Introspector.set(new_value:Value)->this` can be used to set any or all properties of an object if `new_value` is a `ValueTable` specification of new values.
    - `Introspector.to->Value` returns a `ValueTable` containing all properties.
- [API] Added `Object.get_property(name:String)->Value`.
- [API] Added `Object.set_property(name:String,new_value:Value)->this`.
- [API] Added `Object.set_properties(new_values:Value)->this` (`new_values` should be a ValueTable).
- [API] Added `.to->Value` conversion to `Object`, `String`, `List`, and `Table` as well as all primitives and compounds.  Lists convert into `ValueList` objects while objects and tables convert into `ValueTable` objects.
- [Rogue] Any `Object` method such as `object_id()->Int64` can now be called on any aspect reference.
- [Rogue] Any `Object` conversions such as `->String` can now be performed on any aspect reference.
- [RogueC] Fixed bug calling methods on aspect references when the methods are declared `[abstract]` in the aspect.
- [RogueC] Fixed bug resolving escaped local variable names in inline native code when there are multiple locals with the same name.

### v1.0.87 - July 31, 2016
- [Rogue] Added alternate syntax for defining setter methods: `method set-x(...)` is an alternate form of `method set_x(...)`.  In both cases the true name of the method is `set_x`.  The purpose of the hyphenated syntax is to keep `x` visible to search-and-replace.  For example, if you renamed a property from `alpha` to `opacity`, it's easy to miss `set_alpha`.
- [API] Fixed logical error in `Character.to_number(base=10:Int32)->Int32`.
- [API] JSON-style Value types can now be compared with String types and all primitive types.

### v1.0.86 - July 29, 2016
- [Rogue] Added `--define="name:value"` compiler directive.  Example: `--define="PI:Math.acos(-1)"`
- [Rogue] An undefined identifier in a conditional compilation `$if` now counts as false.
- [RogueC] Fixed compiler crash when a generic function specifies the wrong number of parameters compared to the specific function it's matched to.

### v1.0.85 - July 26, 2016
- [API] Added `Timer.restart(new_duration:Real64)` in addition to existing `Timer.restart()`.

### v1.0.84 - July 25, 2016
- [RogueC] Improved call resolution with named arguments.  If there are still multiple candidates after the previously existing method selection logic, the candidate list is refined by keeping only methods that have enough non-default parameters without counting named arguments.  For instance, a call "fn(true,&flag)" would previously have matched both `fn(Logical,&flag)` and `fn(Logical,Logical,&flag)` since both methods accept two arguments, but when `&flag` is removed from consideration there is only one method that accepts a single argument.
- [RogueC] Significant change to the ordering of type evaluation after discovering that base classes containing extended class properties (e.g. `Class Pet / PROPERTIES / cat : Cat`) could easily cause compiler crashes (if `Pet` happened to be processed before `Cat` in the code).  Now all types are recursively `configure()`d as a first step, which instantiates templates and creates type hierarchies for all properties and methods.  Then all terminal (unextended) classes are `organize()`d, which recursively organizes base types and builds method tables.  Finally all classes are `resolve()`d, which evaluates method body statements.  The whole process is repeated if new classes are introduced along the way.

### v1.0.83 - July 24, 2016
- [Rogue] Global aspect methods now work again.
- [Rogue] When a class and an aspect the class incorporates both define the same method and the aspect method's return type is `this`, the class method trumps the aspect method (as before) but now the aspect method return type can be more general than the class method return type (as you would expect).
- [API] Added efficient implementations of `FileWriter.write(String)`, `FileWriter.write(StringBuilder)`, and `File.save(StringBuilder)`.
- [API] Made implementations of `File.save(String)` and `File.save(Byte[])` more efficient.
- [API] `PrintWriter(Writer<<Byte>>)->PrintWriter` can be used to create an object that wraps the given writer in a PrintWriter object.
- [API] Tweaked PrintWriter implementation.

### v1.0.82 - July 24, 2016
- [Rogue] Generic functions definitions must now use `=>` rather than `=`, e.g. `list.sort( function(a,b)=>(a<b) )`.  `=>` has been the official symbol for some time now; `=` was unintentially left in.
- [API] Added `List.count(Function($DataType)->Logical)->Int32` that counts the number of items that pass the test function.

### v1.0.81 - July 17, 2016
- [RogueC] Fixed try/catch scoping error in generated C++ code when it makes use of tasks (broken in v1.0.80).
- [C++] Replaced RogueCPPException with `ROGUE_RETAIN_CATCH_VAR()` macro.

### v1.0.80 - July 17, 2016
- [RogueC] Fixed order-of-evaluation bug where type `Global` was not necessarily `organize`d before resolving code that implicitly uses the `Global` context.  Example: a call to `println` in class `Exception` or one of the primitives would not resolve and only an explicit `Global.println` would work.
- [RogueC] Generated C++ code now wraps `Rogue_launch()` and `Rogue_update_tasks()` in `try/catch(RogueException)/RogueException__display()` handlers.
- [C++] Added `RogueException__display(RogueException*)` (requisite Rogue method) as a simple way of displaying a caught error and the accompanying stack trace if present.
- [C++] Removed vestigial `Rogue_error_object` from runtime framework.
- [C++] In C++, base class `Exception` is now `RogueException` rather than `RogueClassException`.
- [API] Added `sign()` to `Real` and `Int` types that returns `-1`, `0`, or `1` of the appropriate type.
- [API] `InsertionSort` no longer creates a new reader object every time it is called.
- [API] Added `BubbleSort` (`BubbleSort<<T>>.sort(list:T[], compare_fn:Function(T,T)->Logical)->T[]`).

### v1.0.79 - July 16, 2016
- [API] `Real64Value` objects now omit ".0" when printing their value or converting it to a string.
- [API] Added `Real64`/`Real32` methods `whole_part()->RealX` and `fractional_part()->RealX`.
- [API] Added `Real64`/`Real32` method `ceiling()->RealX`.
- [API] `Real64`/`Real32` method `format([decimal_digits])->String` now allows an unlimited number of decimal digits (0+) when no argument is passed instead of the previous cap of 4 digits.
- [API] In the JSON-style Value system, removed class `Int32Value` so that all numbers are stored as `Real64Value` objects.
- [RogueC] Improved method call resolution.

### v1.0.78 - July 15, 2016
- [RogueC] Renamed `Program.is_type_defined()` check to `Program.is_type_used()` (when deciding whether or not to inject code referencing class `TaskManager`) to work correctly if the type has been defined and then culled.
- [RogueC] Fixed check for attempting to compare two compounds (with `==`, `<`, etc.) to require an `operator<(CompoundType)` method if the operator is not `==` or `!=` (an `operator==()` comparison is built-in to compounds).
- [API] Added `Console.peek()->Byte` to implement missing abstract method incorporated from `Reader<<Byte>>`.

### v1.0.77 - July 12, 2016
- [RogueC] C++ definition of base `struct RogueObject` now injects preprocessor value `ROGUE_CUSTOM_OBJECT_PROPERTY` if it's defined, allowing for one or more custom properties per object.
- [RogueC] Fixed object conversions between related classes to look for `CurType.to->NewType` and then `NewType.init(CurType)` methods before defaulting to typecasts.  The bug was introduced when the code for handling conversions was merged with the code for handling typecasts some time ago.

### v1.0.76 - July 11, 2016
- Integrated changes from MurphyMC
    - Standard.Set: Add `is_empty()`
    - CPPWriter: normalize select expressions
    - Makefile: Changes (mostly for installation)
    - NativeCPP: Set platform macro on Linux
    - RogueC: More `--requisite*` help text
    - Exception: Make base methods requisite
    - NativeCPP: Fix exceptions with `--gc=auto`

### v1.0.75 - July 11, 2016
- [Rogue] Removed `isNot` operator in favor of two-word operator `is not`.
- [Rogue] Removed `notInstanceOf` operator in favor of two-word operator `not instanceOf`.
- [API] Renamed `trimmed(left=0:Int32,right=0:Int32)->String` to `clipped(left=0:Int32,right=0:Int32)->String`.  `trimmed()->String` still exists and behaves similarly to Java's `trim()`.


### v1.0.74 - July 7, 2016
- [RogueC] Changed initial, automatic include of the Standard Library to only include the single new file "NativeCode.rogue", which in turn decides which native files to include.  The full standard library include then happens at the end, as in pre-v1.0.72.  This prevents "Standard/" from being added to the default filename search path so that a project attempting to include its own source file won't accidentally get the Standard Library version of the file if they happen to have the same name.
- [Standard] Removed vestigial "Event.rogue" which exposed the "same filename, wrong library" bug mentioned above.
- [Standard]  Made the Standard Library includes more explicit: "Standard/List.rogue" instead of just "List.rogue", etc.

### v1.0.73 - July 7, 2016
- [RogueC] Improved `prior.init` call resolution.
- [RogueC] The compiler now performs an internal `$include "Standard"` first rather than last, ensuring that the code in `NativeCPP.h` always comes first in the output C++.


### v1.0.72 - July 6, 2016
- [Rogue]  A `loop` with a specific number of iterations now has a single-line variant:

    # Multi-line
    loop (2)
      println "pizza"
    endLoop

    # Single-line
    loop (2) println "good"

### v1.0.71 - July 5, 2016
- [RogueC] Fixed bug in overloaded routine mechanism.
- [RogueC] CmdContingent now omits unused escape label to fix unused label warning.

### v1.0.70 - July 4, 2016
- Merge pull request #7 from `MurphyMc/misc_work_march2016`
    - Misc work March 2016 by MurphyMc
    - Add additional plugin hooks 42813c4
    - Type: Add `is_function` tag 4493e0e
    - stdlib: Remove unused RogueException    fe76c01
    - stdlib.Exception: Add .format() 6bdf367
    - CPPWriter: Generate debug trace info more robustly    41633e5
    - primitives: Add .minimum and .maximum for Int32 / Suggest this is done for all primitive numeric types.  839bde3
    - Add/improve a couple useful `to->Strings`    0a3ea9d
    - CPPWriter: Don't `assign_cpp_name` more than once    3d9bfd1
    - CPPWriter: Include original type name as a comment aa6ccc7
    - Exception: Set Exception message by default 0aa84a3
    - Parser: Add templates to Program earlier 4a9db80
    - Ability to generate virtual/dynamic calls for all methods de94726
    - Requisite rework and api attribute
    - Removed odd (accidental?) `->String` return type on `RogueC.include_source()`


### v1.0.69 - July 4, 2016
- [RogueC] Added some kludgy code to get rid of C++ "unreachable code" warnings when all control paths in a `contingent` return a value.


### v1.0.68 - July 3, 2016
- [Rogue]  Added ability to define anonymous inline classes:

    local obj = SomeClass() instance
      # PROPERTIES, METHODS, ...
    endInstance

    local obj = SomeClass(initializer-args) instance(initializer-parameters)
      # PROPERTIES, METHODS, ...
    endInstance

- [Rogue]  Added new `forEach` option that allows a local variable name to be specified for the collection being iterated over.

    # Old Code
    local rewriter = list.rewriter
    forEach (value in rewriter)
      if (passes_test(value)) rewriter.write( value )
    endForEach

    # New Code
    forEach (value in rewriter=list.rewriter)
      if (passes_test(value)) rewriter.write( value )
    endForEach

- [Rogue] Aspects can now specify a base class.  When a class incorporates an aspect with a base class it is as if the class directly extends the base class.
- [Rogue] Optional primitive types are now compatible with other optional primitive types.  In other words an `Int32?` can be passed in for a `Real64?` parameter.
- [API]  Renamed `List.rebuilder()->ListRebuilder<<$DataType>>` to `List.rewriter()->ListRewriter<<$DataType>>`.
- [API]  `Degrees` and `Radians` compounds now implement relational operators (`==`, `<`, etc.).
- [RogueC] Fixed issue causing error message "<VisitorType> does not overload method visit(CmdModifyAndAssign)."
- [RogueC] Modify-and-assign operations now correctly map to global operator methods for locals and globals.

### v1.0.67 - June 28, 2016
- [Rogue] Changed generic function result definition symbol from `=` to `=>`.  Example: `list.sort( function(a,b) => a < b )`.
- [Rogue] Changed directives `$includeNativeHeader` and `$includeNativeCode` into dependency statements `includeNativeHeader` and `includeNativeCode`.  Consequently native includes happen during analysis rather than parsing.
- [Rogue] Changed `Character` to be a signed instead of unsigned 32-bit signed integer to simplify mixed Int32/Character operations and make Rogue code more portable (e.g. to Java/JavaScript someday).  The supported range of characters remains 0..0x10FFFF (21 bits).
- [RogueC] Compiler now correctly finds global method `operator+(Type,Type)` as well as regular method `operator+(Type)`.
- [API] `Console` now incorporates the `Reader<<Byte>>` aspect for reading from standard in (`has_another->Logical`, `read->Byte`).  Also added `Console.read_line->String` that returns the next line after stripping the newline character.  All calls are blocking.

### v1.0.66 - June 19, 2016
- [Rogue] Added `//` convenience operator that converts `obj//key` into an element access `obj["key"]`.
- [Rogue] Added two shorthand versions of `try`: the statement `try single; line; commands` runs the given commands while silently catching and ignoring any thrown exception.  The expression `try <try-expression> else <else-expression>` evaluates to the result of `<try-expression>` unless that expression throws an error, in which case the result of evaluation is `<else-expression>`.
- [API] Simplified type conversion in Value classes - `table.string["key"]` is now `table["key"]->String` etc.
- [RogueC] Fixed a crash bug when parsing `return` outside of a method definition.
- [RogueC] Non-generic function definitions now have an implicit type for type inferencing.

### v1.0.65 - June 17, 2016
- [Rogue] A `select` value that is both condition and result is now only evaluated once.  For example, if some sequence counter will give a next value of `1`, then `select{counter.next:counter.next || -1}` will result in `2` but `select{counter.next || -1}` will result in 1.
- [Rogue] Reversed the meaning of `trace` and `@trace`.  `trace` now includes the method signature, filename, and line number while `@trace` omits them.  Think Makefiles with `@` omitting output.  A trace without any arguments still prints out the location whether or not it has an `@`, just as before.
- [Rogue] Classes can now have a global method `operator?( obj:<ObjectType> )->Logical`.  The default behavior when an object is logicalized is to convert `if (obj)` to be `if (obj isNot null)`.  If you provide that method, however, it will convert to `if (<ObjectType>.operator?(obj))`.
- [RogueC] Fixed extended classes to call `prior.init_object()` before instead of after assigning their own initial property values.  This allows initial property values to be overridden in extended classes.
- [API] Fixed bug `List.insert(other:List,before_index=0:Int32)->this`.  The entire contents of the `other` list's backing array were being addd to the current list.
- [API] The Value system now uses a NullValue singleton instead of actual null values.  All Value types returned collection accesses will be non-null (but may be NullValue instead).  `value.is_null` and `value.is_not_null` can be used to check value's nullness.
- [API] Added comparison operators for `Value` types and implemented `ValueList.contains(String|Value)`.
- [API] `ValueList/ValueTable.load(File)` now accepts a null file.
- [API] JSON loading now consolidates strings and identifiers.
- [API] Renamed introspection methods `PropertyInfo.property_name()` and `PropertyInfo.property_type_info()` to be `name()` and `type()` instead.

### v1.0.64 - June 11, 2016
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

### v1.0.63 - May 28, 2016
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

### v1.0.62 - May 24, 2016
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

### v1.0.61 - May 21, 2016
- [API] Renamed JSON-style data `PropertyValue`, `PropertyList`, and `PropertyTable` to be `Value`, `ValueList`, and `ValueTable` instead.
- [Rogue] The symbols `@[]` (ValueList) and `@{}` (ValueTable) can now be used as types.  For example, declaring a property `table : @{}` is equivalent to `table : ValueTable`, while `table = @{}` is equivalent to `table = ValueTable()`.
- [Rogue] Macro methods are now `[propagated]` automatically so that they resolve in the most specific object context before being substituted in place of the call.
- [Rogue] `catch` can now omit the variable name if the variable isn't used and just specify an exception type.  For example, `catch (err:Error)` can be written `catch(Error)`.
- [Rogue] Improved generated code of try/catch system to handle multiple 'catch' levels.
- [API] Fixed Value add() macro methods to return 'this' instead of Value so that they have the correct return type when called on ValueList.
- [RogueC] Exception types specified in `catch` are now marked as 'used' types in the event that those types aren't referenced anywhere else.
- [RogueC] Added `-Wall` (enable all warnings) option to clang when roguec is run with `--compile`.

### v1.0.60 - May 19, 2016
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


### v1.0.59 - April 30, 2016
- [Rogue] Reworked introspection so that lists of properties and global properties are stored in TypeInfo objects instead of being obtained from instanced objects.  For example, `@Alpha.properties` and `@Alpha.global_properties` access corresponding `PropertyInfo[]` lists.
- [Rogue] Reworked introspection setters and getters to use runtime data rather than generating new methods Rogue-side.
- [Rogue] Global properties now have getters and setters via `TypeInfo` objects that behave like object introspection methods.  For instance, `@SomeClass.set_global_property<<Int32>>("x",3)`.
- [API] Shifted `collect_garbage()` and all introspection-related methods out of System class and into the Runtime class.
- [Rogue] Added new postfix unary operator `isReference` that returns true if its operand type is a class or aspect reference type and false if it is a compound or primitive value type.  Useful in type and method template definitions.  Example: `logical result = $DataType isReference`
- [Rogue] 'if' statement conditions that resolve to a literal `true` or `false` now clear the statement list of the opposing section, both as an optimization and as a way to write template code where `C++` would be unable to compile both sections of an type-based `if` statement.  Note that elseIfs are automatically converted to nested if/else blocks.  Also note that `if` statements in tasks may not receive this optimization.
- [Rogue] `PropertyInfo` objects now contain `property_name_index:Int32` and `property_type_index:Int32` as their core data and call `Runtime.literal_string(index:Int32)` and `Runtime.type_info(index:Int32)` to obtain property name strings and type info objects.

### v1.0.58 - April 27, 2016
- [RogueC] Fixed order-of-compilation bug relating to property types not being `organize()d` before property introspection methods were generated.
- [RogueC] Property introspection access methods now only handle properties defined in the current class and call the prior base class method to access any inherited properties.
- [Rogue] Each class's implicit `init_object()` method now calls `prior.init_object()` as its first step.  Inherited properties are now initialized by the prior method call and are not initialized in the extended class's `init_object()` method.

### v1.0.57 - April 26, 2016
- [Rogue] Changed property introspection setters to return `this` instead of nil.
- [Rogue] Compound property introspection setters now have the same signature as regular class setters.  However, the result must be reassigned to permanently change the compound.  For example, `local xy = XY(3,4); xy = xy.set_property<<Real64>>("y",6); println xy  # prints (3,6)`
- [RogueC] Compounds may incorporate aspects (as before) but now the compiler does not consider them polymorphically compatible with those aspect types.
- [RogueC] When a method specifies `this` as its return type, the method gains `Attribute.returns_this`.  References to `this` in aspects, including return values, are now converted into the incorporating type instead of remaining as the aspect type.

### v1.0.56 - April 25, 2016
- [Rogue] Added property introspection.  `obj.type_properties()->PropertyInfo[]` returns a list of property info objects.  For any given property info object one can access `info.property_name->String`, `info.property_type_name->String`, and `info.property_type_info->TypeInfo`.  To get and set class object properties through introspection, use `obj.get_property<<DataType>>(name:String)->DataType` and `obj.set_property<<DataType>>(name:String,new_value:DataType)`.  Due to the nature of compounds they use a different setter mechanism: write `compound_value = CompoundType.set_property<<PropertyType>>( compound_value, "property_name", new_property_value )`.
- [RogueC] Revamped method template system to be more robust and flexible, including: classes can now override specific versions of method templates as well as using specialized template syntax in method names even when no such template exists.  For instance, `method frobozz<<Int32>>(...)` overrides any inherited definition of template `method frobozz<<$DataType>>`.
- [API] Added an optional `allow_break_after:String` parameter to `String.word_wrapped()`.  If a line can't be broken on a newline or space it will be broken after any one of the supplied characters - the Rogue compiler specifies "," to break up long signature names.
- [RogueC] Improved formatting of error messages in several ways, including that "method not found" errors now strip package names from signature to avoid bloat.
- [Rogue] `trace` can now be used with formatted strings, e.g. `@trace "sum=$" (x+y)`.
- [Rogue] Any methods overriding `requisite` methods now inherit the `requisite` attribute.

### v1.0.55 - April 18, 2016
- [Rogue] Added method templates.  These operate as you might expect: method templates are defined with `method name<<$Type1,...>>(...)` and called with e.g. `obj.name<<Int32>>(...)`.
- [Rogue] Added `ensure` statement.  `ensure x` is convenience syntax for `if (not x) x = TypeOfX()`, `ensure y(a,b)` is equivalent to `if (not y) y = TypeOfY(a,b)`, and `ensure x && y(a,b)` performs both checks in consecutive order.
- [RogueC] Made named args more robust.  While the compiler used to wait until a method was selected before adding the named args back in, it now inserts them at the beginning of call resolution if it can infer their location (i.e. two overloads don't contain the same parameter name at different positions).
- [API] Made functional programming more flexible.  In addition to the in-place list modification methods `apply()`, `filter()`, `modify()`, and `sort()`, there are now versions of those methods that return a modified list instead: `applying()`, `filtered()`, `modified()`, and `sorted()`.  Note: `filtered()` is functionally equivalent to `choose()` and so the latter method has been removed.
- [API] Map/reduce functionality has been shifted from "helper classes" into List proper, made possible by method templates.  Their names are `mapped` and `reduced`, which is consistent with other functional programming methods in Rogue.
- [API] Added `List.remove(Function(T)->Logical)->List` that removes and returns the list of values passing the test function.
- [RogueC] Added method `Type.inject_method(Method)` that can be used to add a new method after the type has already been organized, correctly adjusting inherited method tables in extended classes.
- [RogueC] Creating an object as a standalone statement now omits the post-`init_object()` typecast which prevents a C++ warning.
- [RogueC] Renamed "flag args" to "named args" internally.

### v1.0.54 - April 13, 2016
- [RogueC] Reworked native properties to be stored with regular properties instead separately.  Fixes severe bug where classes with native properties were not polymorphically compatible with base classes or extended classes due to the order in which native properties were written out.
- [Rogue] A value being explicitly or implicitly converted (cast) now checks for to-type constructor methods as well as from-type conversion methods.  So for `x:FromType -> ToType", the compiler first checks for a method `x->ToType` and then checks for `ToType.init(FromType)` or `ToType.create(FromType)`, resulting in a conversion to `ToType(x)` if that method is found.
- [RogueC] Fixed some lingering references where global methods were called "routines" (their old name).

### v1.0.53 - April 11, 2016
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

### v1.0.52 - April 10, 2016
- [Rogue] Added preliminary introspection support. `@TypeName` is a convenience method for calling `System.type_info("TypeName")` that returns a `TypeInfo` object with a `name` property.  You can call `.create_object()->Object` on a TypeInfo object, cast the Object to a specific type, and manually call one of the specific type's `init()` methods.
- [Rogue] Added `Object.type_info()->TypeInfo` that returns the runtime type (AKA "Class") of an object.
- [Rogue] C++ now has a function `RogueType_type_info(RogueType*)->RogueTypeInfo*` that returns the runtime type info of an object.
- [RogueC] CmdBinary and CmdUnary now call organize() on the operand types before proceeding to ensure that operator methods can be found if they exist.

### v1.0.51 - April 6, 2016
- [Rogue] `assert` now accepts an optional custom message: `assert(condition||message)`.
- [Rogue] `assert(condition[||message])` may now be used as part of expression - for example, `q = a / assert(b)`.  `b` is returned if it is "truthy" or if `--debug` is not enabled.
- [Rogue] Added `require(condition[||message])` that works just like `assert` except that it is always compiled in and does not depend on `--debug` mode.  A failed requirement will throw a `RequirementError`.
- [Rogue] Added class and method level `unitTest...endUnitTest` support (also single-line `unitTest ...`).  Tests are compiled and run at launch if `--test` is passed to `roguec` and if the containing class is used in the program.
- [Rogue] Added `[propagated]` method qualifier.  Propagated methods are cloned each time they are inherited so that their `this` reference has the correct type.  This is primarily useful when implementing the Visitor pattern.
- [RogueC] Implemented the Visitor pattern for misc. tasks.  Removed `CmdAugment*.rogue` in favor of `TraceUsedCodeVisitor.rogue` and `UpdateThisTypeVisitor.rogue` - much, much, simpler.
- [RogueC] Tweaked CPPWriter's `print_arg()` to automatically cast the arg type to the param type if they differ.
- [Standard Library] Used `--requisite` to find and fix some errors that had crept in due to other revamps.

### v1.0.50 - April 5, 2016
- [Rogue] Added `assert(condition)` statement.  When `--debug` is enabled an AssertionError will be thrown if the condition is false.  When `--debug` is not enabled the assert is stripped during compilation.
- [C++] `RogueObject_to_string(obj)` can now be called to invoke any object's `to->String()` method.  If `to->String()` was culled during compilation then the object's class name will be returned.  Exception `to->String()` methods are automatically marked `[requisite]`.
- [C++] The `to->String()` of any uncaught exceptions is now displayed at runtime.
- [RogueC] Nil returns in `init()` methods are now automatically converted to `return this`.
- [RogueC] In mixed Int32-Character operations, Int32 is now promoted to Character rather than vice versa.  Before, `Character(-1) > 0)` returned `false` even though Character is unsigned because it was being converted back to Int32 during the comparison.
- [Syntax] Updated Vim and Sublime Text 3 syntax files.

### v1.0.49 - April 4, 2016
- [Standard Library] Fixed bugs in `real_bits()` and `integer_bits()`.

### v1.0.48 - April 4, 2016
- [Standard Library] - Moved core functionality for primitive `real_bits()` and `integer_bits()` out of global create methods and into those methods themselves.
- [Standard Library] - Primitives can now be created and cast with constructor-style syntax, e.g. `n = Int32(3.4)`.
- [Standard Library] - Printing an out-of-range Character code now prints a question mark (?).
- [RogueC] - Stand-alone ranges (outside of forEach loops) now accept a `step` size.
- [RogueC] - If a range has no explicit step size then its default step type now matches the range type.

### v1.0.47 - April 2, 2016
- [Standard Library] - Added `String.up_to_first()` and `.up_to_last()` methods that are inclusive versions of `.before_first/last()`.
- [C++] - Fixed literal string generation to prevent ambiguous hex escapes, e.g. "\x9CCool" is now "\x9C" "Cool"
- [C++] - Renamed RogueString's `previous_byte_offset` and `previous_character_index` to be `cursor_offset` and `cursor_index`.
- [C++] - Added new method `RogueString_set_cursor(THIS,index)->RogueInt32` that takes a character index, sets the cached `cursor_offset` and `cursor_index` in the string, and returns `cursor_offset`.

### v1.0.46 - April 1, 2016
- Converted String and StringBuilder to use utf8 bytes instead of a Character[] array internally while using character indices externally.
- Changed Character to be an unsigned 32-bit value.
- All string indexing and substring operations operate on whole characters (code points 0 through 10FFFF); characters never span multiple indices though the internal UTF8 representation has a variable size.
- Added classes `UTF8Reader:Reader<<Character>>` and `UTF8Writer:Writer<<Character>>` that wrap `Byte` readers and writers to provide UTF-8 decoding and encoding.
- `File.reader()` and `File.writer()` now return raw byte readers and writers while `File.character_reader()` and `File.character_writer()` return UTF-8 readers and writers.
- Fixed named parameters to work with routines.
- Removed [foreign] qualifier on String.
- Renamed C++ method `RogueString_create_from_c_string()` to `RogueString_create_from_utf8()`.
- Removed C++ method `RogueString_to_c_string()`; a RogueString's `utf8` property is a null-terminated UTF-8 string that can be cast directly to `const char*`.


### v1.0.45 - March 26, 2016
- Fixed `Table` crash when `remove()` is called with a key that hashes to an unused bin.

### v1.0.44 - March 24, 2016
- Restored default `==` and `!=` functionality to compounds.  No operator methods are required for equivalence comparisons.

### v1.0.43 - March 23, 2016
- During call resolution, candidate methods not containing any given named args as parameters are discarded early.
- Fixed additional PROPERTIES in compounds to be initialized correctly.
- Compounds now generate an appropriate error message when operator methods are required but undefined (such as `operator==`).

### v1.0.42 - March 23, 2016
- Fixed runtime instanceOf to return `false` instead of `true` for null references.
- Compounds can now be declared without properties.

### v1.0.41 - March 22, 2016
- Routines can now be overloaded.
- Augments now work correctly again - augments to all specialized types (`Table`) and to specific specialized types (`Table<<String,Int32>>`) are now applied correctly.
- Fixed mysterious "unexpected end of line" error relating to augments - augments use special tokens indicating the beginning of augment code.  Those tokens were labeled "end of line" (for some reason) and their application was slightly messed up.

### v1.0.40 - March 20, 2016
- Changed C++ name adaptation syntax: `TemplateName<<Alpha,Beta>>` now becomes `TemplateName_Alpha_Beta_` and `TypeName[]` becomes `TypeName_List`.

### v1.0.39 - March 20, 2016
- Added default `init()` to base class Object.
- Classes can no longer be instantiated with a default initializer if they have other initializers all with one or more args.
- Added `requisite` target to `Makefile` that recompiles with the `--requisite` flag.
- Fixed bug where create() method candidates could be prematurely discarded from consideration in favor of `init()` methods with the same signature.
- Tweaked the C++ name adaptation and validation to make it more robust and reduce the chances of conflict.  `TemplateName<<Alpha,Beta>>` now becomes `Template$Alpha_Beta$` and `TypeName[]` becomes `TypeName$List`.

### v1.0.38 - March 19, 2016
- Added `block`/`endBlock` control structure that scopes local variables and supports `escapeBlock`.

### v1.0.37 - March 18, 2016
- Improved call resolution.
- For a call to `NewType(x)`, methods named `init()` and global methods named create() are all considered simultaneously instead of in two separate steps as before.
- If all else fails during call resolution the compiler will try and convert the arg of a 1-arg call `to->Object`, `to->String`, or `to->ParameterType` in that order (missing from previous release but present before that).

### v1.0.36 - March 18, 2016
- Fixed overly liberal method selection so that for e.g. constructor `NewType(String)`, `create(String)` will be matched before `init(Int32)` (this latter wasn't throwing an error due to String having a `to->Int32` method).
- Fixed runtime `as` and `instanceOf` to work with more than direct parent class checks.
- Moved creation of compiler's `flat_base_types` to be on-demand when generating code.

### v1.0.35 - March 18, 2016
- Reworked debug trace infrastructure; now substantially faster and more flexible - 1.3s vs 1.7s roguec recompile in debug mode.
- Debug Stack Trace now shows accurate line numbers.
- Fixed access resolution error where class `Global` could be checked before the current class context.
- Fixed several errors resulting from compiling with `--requisite`.

### v1.0.34 - March 18, 2016
- C++ exceptions and stdlib improvements.
- Fixed access resolution error where class `Global` could be checked before the current class context.
- Fixed several errors resulting from compiling with `--requisite`.

### v1.0.33 - March 17, 2016
- Aspects are now compatible with Object, instanceOf Object, and can be be cast to Object in more places.
- Added fallback property getters and setters to aspects.

### v1.0.32 - March 17, 2016
- Added `consolidated()->String` method to String and StringBuilder.  Operates similarly to Java's intern() and maps equivalent strings to a single string object instance.
- Compiler now generates global properties for native classes and initializes them with the class's `init_class()` call.
- Class File now includes native header `<cstdio>` when referenced.

### v1.0.31 - March 16, 2016
- Renamed the methods of the plug-in architecture.

### v1.0.30 - March 16, 2016
- Added beginnings of compiler plug-in infrastructure.
- Added `--version` directive.
- Added explicit `[native]` attribute to type Object - the compiler was already doing so internally.
- Removed unused, vestigial copy of global properties generated as static variables in class definitions.

### v1.0.29 - March 15, 2016
- Improved `select{}` syntax - `||` is used to separate all parts (`select{ a:x || b:y || c:z || d }`) and `x` may be used instead of both `x:x` and `x.exists:x:value` (if `x` is an optional type).
- Added *up to less than* range operator `..<`.  For instance, `forEach (i in 0..<count)` loops from `0` to `count-1`.

### v1.0.28 - March 14, 2016
- When used in implicit typing, routine calls now correctly report the return type instead of the type of the routine wrapper class.

### v1.0.27 - March 14, 2016
- Added `String.contains(Character)->Logical`.
- Renamed `String.pluralize()` to `String.pluralized()` for consistency.
- Moved `Boss.rogue` to its own library to avoid cluttering up the Standard library API.
- Overridden methods are now culled when they're unused.

### v1.0.26 - March 14, 2016
- Fixed `[requisite]` routines to work correctly when the routine has a return type.
- Renamed `[functional]` to `[foreign]`.  A `[foreign]` class is one that supplements a native non-Rogue reference type with methods callable from Rogue.  This feature is still somewhat hard-coded for the C++ implementation of Rogue `String`.
- Fixed method overriding to make sure the overriding method's return type is instanceOf the overridden method's return type instead of short-circuiting when the overridden method is abstract.
- Tightened up `reset()` and `seek()` in various readers and writers, fixing a few potential bugs in the process.

### v1.0.25 - March 13, 2016
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

### v1.0.24 - March 11, 2016
- Added '--gc=boehm' support for Boehm's garbage collector, courtesy Murphy McCauley.
- Added '--gc-threshold={number}[MB|K]' and increased default to 1MB.
- Changed the allocation tracker to count down from the threshold to 0 instead of counting up from 0 to the threshold.
- Fixed bug mapping variable writes to `set_` methods.

### v1.0.23 - March 10, 2016
- Integrated MurphyMc's gc option which allows GC to happen during execution rather than only between calls into the Rogue system.
- Fixed a reference holding error in List.

### v1.0.22 - March 9, 2016
- `++` and `--` now work on compounds by transforming `++compound` into `compound = compound.operator+(1)` and `--compound` into `compound.operator+(-1)`.
- Added full set of `operator` methods to `Degrees` and `Radians` compounds.  Also added `Degrees.delta_to(other:Degrees)` which returns the smallest angle (which may be positive or negative) required to to from this angle to the specified other angle.  For instance, `Degrees(270).delta_to(Degrees(0))` returns `Degrees(90)`.
- Added an elapsed time output when a compile is successful.
- Lists now have a default capacity of zero to begin with and do not allocate a backing array.  When the first item is added an initial capacity of at last 10 is reserved.

### v1.0.21 - March 8, 2016
- Changed syntax of `select{ condition:value, default_value }` to `select{ condition:value || default_value }`.
- Removed support for compound modify-and-assign operators since they're only changing a stack copy of the compound.  Rogue now automatically converts "compound += value" to "compound = compound + value".

### v1.0.20 - March 8, 2016
- `operator+=` and other modify-and-assign operator methods now work with compounds as well as regular class types.
- `System.environment` provides several ways to operate environment variables: `.listing()->String[]`, `get(name:String)->String`, and `set(name:String,value:String)`.  A small example:

    System.environment[ "STUFF" ] = "Whatever"

    forEach (var_name in System.environment.listing)
      println "$ = $" (var_name,System.environment[var_name])
    endForEach

    System.environment[ "STUFF" ] = null


### v1.0.19 - March 8, 2016
- Aspect types now correctly extend RogueObject in C++ code.

### v1.0.18 - March 8, 2016
- `RogueObject_retain()` and `RogueObject_release()` now guard against `NULL`.
- Renamed `STL` library to `CPP`.

### v1.0.17 - March 7, 2016
- Objects requiring clean-up are no longer freed if they have non-zero reference counts.

### v1.0.16 - March 7, 2016
- Removed trailing whitespace in all source files.

### v1.0.15 - March 7, 2016
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


### v1.0.14 - March 7, 2016
- Renamed `IntX.as_realx()` and `RealX.as_intx()` to be `IntX.real_bits()` and `RealX.integer_bits()`.
- Fixed GC code generation for compound properties.
- Fixed `local x = x` to throw a syntax error instead of crashing the compiler.
- Fixed compiler crash when parsing a super-minimal class definition such as "class Name;".
- Compiler now ensures that 'as' is only used with references.

### v1.0.13 - March 5, 2016
- Fixed bug where listing a base class after an aspect would corrupt the dynamic dispatch (vtable) ordering.

### v1.0.12 - March 5, 2016
- Made several empty `Reader<<$DataType>>` methods abstract so they don't overwrite existing methods when incorporated in an extended class.

### v1.0.11 - March 5, 2016
- Fixed bug in aspect call mechanism.
- Added `List.choose(Function($DataType)->Logical)->$DataType[]` that returns a list subset.  Example:

    println Int32[][3,4,5,6].choose( function(n)=(n%2==0) )

- Added `StackTrace.to->String` in addition to the existing `StackTrace.print()`.



### v1.0.10 - March 5, 2016
- Added call tracing when the `--debug` option is given.
- Created StackTrace() class and added it to base Exception class.  If `--debug` is enabled then you can `ex.stack_trace.print` when catching an exception.  Seg faults (from referencing null) do not throw exceptions but the stack trace will still print out if `--debug` is enabled.
- Fixed bug when DEFINITIONS have more than one token.
- Tweaked default Reader `seek()` and `skip()` methods.

### v1.0.9 - March 4, 2016
- Fixed up inline native definitions and methods to work with op-assign and increment/decrement.
- Added convenience class `ListLookupTable<<$KeyType,$ValueType>>` where assigning a value to a key adds the value to a list of values instead of replacing a single value.
- Reworked template overloading system.
- Added Real64/32 `is_infinite()`, `is_finite`, `is_number()`, and `is_not_a_number()/is_NaN()` as well as literal values `infinity` and `NaN`.  StringBuilder now prints those values correctly.
- Can now write `@routine_name` to obtain a Function object that calls the routine.
- Added STL library containing `STLPriorityQueue`.
- Added `Set<<$DataType>>` to standard library.


### v1.0.8 - March 3, 2016
- 'select' now uses curly braces instead of square brackets.
- The final case in a `select` can be optionally prefixed with `others:`.  Example: https://gist.github.com/AbePralle/de8e46e025cffd47eea0
- Added support for template overloads (same name, different numbers of specializers).
- Bug fix in template backslash parsing.

### v1.0.7 - March 3, 2016
- Added the `select` operator, Rogue's alternative to the ternary "decision operator".  Example: https://gist.github.com/AbePralle/de8e46e025cffd47eea0
- Added `digit_count()->Int32` to all primitives that returns the number of characters that are required to represent the primitive in text form.
- Added `whole_digit_count()` and `decimal_digit_count()->Int32` to `Real64` and `Real32` primitives.
- Parser bug fix: class auto-initializer properties can contain EOLs.
- Improved macro method error messages.
- Fixed several File macro methods that were incorrectly defined with a `return` keyword.
- Conditional compilation now respects nativeHeader and nativeCode blocks.
- nativeHeader and nativeCode blocks can appear at the global level, the class section level, or the statement level.  In the latter two cases they are only applied if the class or method is referenced, respectively.
- Fixed generated trace() code to work for arrays of compounds that don't contain references.

### v1.0.6 - March 1, 2016
- Added native code marker support for $(var.retain) and $(var.release) that yield the value of the given variable while increasing or decreasing the reference count of regular objects, preventing them from being GC'd while they're stored in a native collection.  The markers yield the variable value with no reference count modifications for primitives and compounds.

### v1.0.5 - March 1, 2016
- Added additional native code insertion marker operations for native properties and inline native code.

  1. `$this`, `$local_var_name`, and `$property_name` are supported as before.
  2. Parens may be added for clarity: `$(this)` etc.
  3. `$(this.type)`, `$(local_var_name.type)`, and `$(property_name.type)` all print out the C++ type name of the given variable.  Parens are required in this case.
  4. `$($SpecializerName)` prints out the C++ type name of a template type specializer.  For instance, in `class Table<<$KeyType,$ValueType>>`, referring to `$($KeyType)` in your inlined native code would write `RogueInt32` for a `Table<<Int32,String>>`, etc.


### v1.0.4 - February 29, 2016
- Fixed off-by-one error in List.remove_at() that was causing seg faults on Linux.
- Updated Makefile.

### v1.0.3 - February 29, 2016
- Renamed `Time.current()` to `System.time()` and removed the `Time` class.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

### v1.0.2 - February 29, 2016
- Adjacent string literal concatenation now works for strings that were template specializers.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

### v1.0.1 - February 28, 2016
- Adjacent string literals are now concatenated into a single string literal.

### v1.0.0 - February 28, 2016
- First full release.

### v0.0.1 (Beta) - May 12, 2015
- Initial beta release.

