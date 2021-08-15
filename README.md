Rogue
=====

About     | Current Release
----------|-----------------------
Version   | 1.9.0
Date      | 2021.09.28
Platforms | Windows, macOS, Linux (Ubuntu+), Cygwin

Note: the build tool [Rogo](https://github.com/AbePralle/Rogo) was formerly bundled with Rogue on Mac and Linux. Both Rogue and Rogo may need to be installed for any projects that list Rogue as a dependency.

## Installation

### macOS and Linux

    make install

### Windows
1. Install Git and Visual Studio 2019 with C++ support.

2. Use a Visual Studio "Developer Command Prompt" for the following and for working with Rogue in general.

3. Clone this repo and:

    make

4. Add the absolute path of the subfolder `Build\RogueC` to the system PATH as directed by the output of the `make.bat` script.

## Syntax Highlighting

### Visual Studio Code

Rogue comes with a work-in-progress VS Code extension which can be found in the `Syntax/VSCode` folder.

### VIM

Rogue comes with syntax and indent modules for Vim; they are located in the Syntax/Vim folder.


#### Notes

1. `roguec` by itself for options.

2. Execute these shell commands for a simple test:

        echo println '"Hello World!"' > Hello.rogue
        roguec Hello.rogue --execute

## License
Rogue is released under the terms of the [MIT License](https://opensource.org/licenses/MIT).

## About
Rogue was created by Abe Pralle in 2015. Abe Pralle and Murphy McCauley lead Rogue's design and development.

