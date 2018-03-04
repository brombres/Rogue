Rogue
=====

About     | Current Release
----------|-----------------------
Version   | v1.3.0
Date      | February 17, 2018
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
Rogue is released under the terms of the [MIT License](https://opensource.org/licenses/MIT).

