all: roguec

roguec: Programs/roguec /usr/local/bin/roguec 

Programs/roguec:
	# Compile existing Source/Compiler/CPP/RogueC.cpp
	mkdir -p Programs
	g++ -Wall -O3 Source/Compiler/CPP/RogueC.cpp -o Programs/roguec

/usr/local/bin/roguec:
	printf "%s\nexec \"%s/Programs/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > /usr/local/bin/roguec
	chmod a+x /usr/local/bin/roguec
