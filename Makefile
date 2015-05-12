ROGUEC_SRC = $(shell find Source/Compiler/Rogue)

all: roguec

roguec: bootstrap_roguec /usr/local/bin/roguec Programs/roguec Source/Compiler/CPP/RogueC.cpp

bootstrap_roguec:
	@if [ ! -f "Programs/roguec" ]; \
	then \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/roguec from C++ source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo g++ -Wall -O3 Source/Compiler/CPP/RogueC.cpp -o Programs/roguec; \
	  g++ -Wall -O3 Source/Compiler/CPP/RogueC.cpp -o Programs/roguec; \
	fi;

Programs/roguec: Source/Compiler/CPP/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	g++ -Wall -O3 Source/Compiler/CPP/RogueC.cpp -o Programs/roguec

/usr/local/bin/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating /usr/local/bin/roguec linked to Programs/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > /usr/local/bin/roguec
	chmod a+x /usr/local/bin/roguec

Source/Compiler/CPP/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/Compiler && roguec Rogue/RogueC.rogue --main --output=CPP/RogueC
