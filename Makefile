.PHONY: test

ROGUEC_SRC = $(shell find Source/RogueC | grep .rogue)
#ROGUEC_FLAGS = -O3

all: roguec

remake: touch_roguec roguec

roguec: bootstrap_roguec /usr/local/bin /usr/local/bin/roguec libraries Source/RogueC/RogueC.cpp Programs/RogueC/roguec

touch_roguec:
	touch Source/RogueC/RogueC.rogue

bootstrap_roguec:
	@if [ ! -f "Programs/RogueC/roguec" ]; \
	then \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/RogueC/roguec from C++ source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo g++ -Wall $(ROGUEC_FLAGS) Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec; \
	  mkdir -p Programs/RogueC; \
	  g++ $(ROGUEC_FLAGS) Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec; \
	fi;

Source/RogueC/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && roguec RogueC.rogue --main --output=RogueC

Programs/RogueC/roguec: Source/RogueC/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/RogueC/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	g++ $(ROGUEC_FLAGS) Source/RogueC/RogueC.cpp -o Programs/RogueC/roguec

libraries:
	@mkdir -p Programs/RogueC
	@rsync -rt --delete Source/Libraries Programs/RogueC

/usr/local/bin:
	sudo mkdir -p /usr/local/bin
	sudo chgrp admin /usr/local /usr/local/bin
	sudo chown $$USER /usr/local/bin


/usr/local/bin/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating /usr/local/bin/roguec linked to Programs/RogueC/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/RogueC/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > /usr/local/bin/roguec
	chmod a+x /usr/local/bin/roguec
	@echo
	@echo Compile Hello.rogue for a simple test:
	@echo
	@echo "  roguec Hello.rogue --main"
	@echo "  g++ Hello.cpp -o hello"
	@echo "  ./hello"
	@echo

test:
	roguec Test.rogue --execute

x2:
	make remake
	sleep 1
	make remake

x3:
	make remake
	sleep 1
	make remake
	sleep 1
	make remake

clean:
	rm -rf Programs/RogueC
	rm -f /usr/local/bin/roguec
	rm -f Hello.cpp
	rm -f Hello.h
	rm -f ./a.out
	rm -f ./hello
