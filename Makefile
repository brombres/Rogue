.PHONY: test

ROGUEC_SRC = $(shell find Source/RogueC | grep .rogue)
ROGUEC_ROGUE_FLAGS =
ROGUEC_CPP_FLAGS = -std=gnu++11 -fno-strict-aliasing -Wno-invalid-offsetof

BINDIR = /usr/local/bin

all: roguec

-include Local.mk

remake: libraries touch_roguec roguec

debug: libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --debug..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC --debug $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

exhaustive: libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --exhaustive..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC --exhaustive $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

roguec: bootstrap_roguec $(BINDIR)/roguec libraries Source/RogueC/Build/RogueC.cpp Programs/RogueC/roguec

touch_roguec:
	touch Source/RogueC/RogueC.rogue

bootstrap_roguec:
	@if [ ! -f "Programs/RogueC/roguec" ]; \
	then \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/RogueC/roguec from C++ source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo $(CXX) -Wall $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec; \
	  mkdir -p Programs/RogueC; \
	  $(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec; \
	fi;

Source/RogueC/Build/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC $(ROGUEC_ROGUE_FLAGS)

Programs/RogueC/roguec: Source/RogueC/Build/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/RogueC/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

libraries:
	@mkdir -p Programs/RogueC
	@if [ $$(rsync -rtv --delete --exclude=.*.sw? --dry-run Source/Libraries Programs/RogueC | wc -l) -gt 4 ]; \
	then \
	  echo "==== Updating Libraries ===="; \
		rsync -rtv --delete --exclude=.*.sw? Source/Libraries Programs/RogueC | tail -n +2 | (tac 2> /dev/null || tail -r) | tail -n +4 | (tac 2> /dev/null || tail -r); \
	fi

libs: libraries

if:
	echo $(COPY_COUNT)

$(BINDIR)/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating $(BINDIR)/roguec linked to Programs/RogueC/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/RogueC/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > roguec.script
	@echo Copying roguec.script to $(BINDIR)/roguec
	@if [[ `uname -s` == "MINGW32_NT"* ]]; then \
	  cp roguec.script $(BINDIR)/roguec; \
	  chmod a+x $(BINDIR)/roguec; \
	else \
	  sudo cp roguec.script $(BINDIR)/roguec; \
	  sudo chmod a+x $(BINDIR)/roguec; \
	fi
	rm roguec.script
	@echo
	@echo You can execute the following as a simple test:
	@echo
	@echo    println '"Hello World!"' > Hello.rogue
	@echo    roguec Hello.rogue --execute
	@echo

test:
	roguec Test.rogue --execute --debug --test

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

revert: revert_cpp_source libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/RogueC/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

revert_cpp_source:
	git checkout Source/RogueC/Build && rm -f Programs/RogueC/roguec

.PHONY: clean
clean:
	rm -rf Programs/RogueC
	rm -f Hello.cpp
	rm -f Hello.h
	rm -f ./a.out
	rm -f ./hello

docs:
	cd Source/DocGen && make

