Bard
====

Bard is an object-oriented, imperative, statically typed general purpose language. In use it feels similar to Java or C# blended with a higher-level language such as JavaScript or Ruby, although its syntax is more reminiscent of Pascal or BASIC.

Like Java and C#, Bard was designed as a more elegant, robust, and flexible alternative to lower-level languages like C and C++ and is suitable for use in the production of full-scale apps such as games, engines, GUI-based tools, command line utilities, and compilers.

Unlike Java and C#, Bard is designed to be fully embedded within the projects and products it's used with. It doesn't require a centralized monolithic install for the developer or and separate installs or updates for the end user. Developers can choose whether to compile a lightweight Bard virtual machine in with their project or to cross-compile Bard source code into the language of their choice (such as C++ and C#).


Status
------
Bard is currently under development.  The language itself is in Beta and the libraries and app programming framework are in Alpha.

As it is currently without documentation, a complete standard library, or a comprehensive build system for different platforms, at this point it is only suitable for curious command line Mac developers.  With that said, to build on Mac:

1.  Clone the repo.
2.  Run "make".
3.  Edit Tests/Test.bard (which is always in flux)
4.  Run "make test"

