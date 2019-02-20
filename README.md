Rogue
=====

About     | Current Release
----------|-----------------------
Version   | v1.4.17
Date      | February 20, 2019
Platforms | Windows, macOS, Linux (Ubuntu+), Cygwin

## Installation

### Mac & Linux
    git clone https://github.com/AbePralle/Rogue.git
    cd Rogue
    make  # bootstraps Rogue, creates 'roguec' and 'rogo' launchers

### Windows
1. Install Git and Visual Studio 2017 with C++ support.

2. Use a VS "Developer Command Prompt" for the following and for working with Rogue in general.

3. Clone the repo and bootstrap Rogue:

    git clone https://github.com/AbePralle/Rogue.git
    cd Rogue
    rogo

4. Add the absolute path of the subfolder `Programs\RogueC` to the system PATH as directed by the `rogo` command.

## Syntax Highlighting

### Visual Studio Code

Rogue comes with a work-in-progress VS Code extension. After installing Rogue, type the following from the base Rogue folder:
```
rogo vscode
```

### VIM

Rogue comes with syntax and indent modules for Vim; they are located in the Syntax/Vim folder.


#### Notes

1. `roguec` by itself for options.

2. Execute these shell commands for a simple test:

        echo println '"Hello World!"' > Hello.rogue
        roguec Hello.rogue --execute

3. `rogo help` for Rogue repo build options.


## License
Rogue is released under the terms of the [MIT License](https://opensource.org/licenses/MIT).

## About
Abe Pralle and Murphy McCauley lead Rogue's design and development.

Rogue was created by Abe Pralle in 2015.

