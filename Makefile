ROGUEC_SRC = $(shell find Source/RogueC | grep .rogue)
ROGUEC_FLAGS = -O1

all: roguec

remake: touch_roguec roguec

roguec: bootstrap_roguec /usr/local/bin/roguec Programs/RogueC/roguec Source/RogueC/RogueC.cpp

touch_roguec:
	touch Source/RogueC/RogueC.rogue

bootstrap_roguec:
	@if [ ! -f "Programs/RogueC/roguec" ]; \
	then \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/RogueC/roguec from C++ source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo g++ -Wall -O3 Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec; \
	  mkdir -p Programs/RogueC; \
	  g++ $(ROGUEC_FLAGS) Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec; \
	fi;

Programs/RogueC/roguec: Source/RogueC/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/RogueC/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	g++ $(ROGUEC_FLAGS) Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec

/usr/local/bin/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating /usr/local/bin/roguec linked to Programs/RogueC/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/RogueC/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > /usr/local/bin/roguec
	chmod a+x /usr/local/bin/roguec

Source/RogueC/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && roguec RogueC.rogue --main --output=RogueC
