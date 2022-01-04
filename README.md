![Rogue Logo](Media/Logo/Rogue-Badge.png)

# Rogue
An ergonomic object-oriented high-level language that compiles to C++.

# Version
- v1.12.2 - January 3, 2022
- macOS, Linux, Windows
- [MIT License](LICENSE)
- By Abe Pralle

# Installation

Install [Morlock](https://morlock.sh).

Installing Morlock will also install Rogue and [Rogo](https://github.com/AbePralle/Rogo).

# Syntax Highlighting

## Visual Studio Code

Rogue comes with a work-in-progress VS Code extension which can be found in the `Syntax/VSCode` folder.

## Vim

Rogue comes with syntax and indent modules for Vim; they are located in the Syntax/Vim folder.


### Notes

1. `roguec` by itself for options.

2. Execute these shell commands for a simple test:

        echo println '"Hello World!"' > Hello.rogue
        roguec Hello.rogue --execute

# License
Rogue is released under the terms of the [MIT License](https://opensource.org/licenses/MIT).

# About
Rogue was created by Abe Pralle in 2015. Abe Pralle and Murphy McCauley lead Rogue's design and development.

