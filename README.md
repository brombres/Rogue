Rogue
=====
- v1.0.4
- February 29, 2016

## Installation (Mac, maybe Linux)
    git clone git@github.com:Plasmaworks/Rogue.git
    cd Rogue
    make

**NOTES**

1. Running make will compile the `roguec` compiler from C++ source and create the executable script `/usr/local/bin/roguec`

2. Run `roguec` by itself for options.

3.  Run this for a simple test:

        echo println '"Hello World!"' > Hello.rogue
        roguec Hello.rogue --execute

## Change Log
**v1.0.4** - February 29, 2016
- Fixed off-by-one error in List.remove_at() that was causing seg faults on Linux.
- Updated Makefile.

**v1.0.3** - February 29, 2016
- Renamed `Time.current()` to `System.time()` and removed the `Time` class.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

**v1.0.2** - February 29, 2016
- Adjacent string literal concatenation now works for strings that were template specializers.
- Default main() now GC's 3 times at the end to allow objects requiring clean-up a chance to do so.

**v1.0.1** - February 28, 2016
- Adjacent string literals are now concatenated into a single string literal.

**v1.0.0** - February 28, 2016
- Official release.

**v0.0.1 (Beta)** - May 12, 2015
- Initial beta release.

## About
The Rogue programming language is the successor to Slag (active years 2008-2011) and Bard (never released; multiple internal versions made in 2011-2014).

## License
Rogue is released into the Public Domain under the terms of the [Unlicense](http://unlicense.org/).

