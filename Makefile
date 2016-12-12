.PHONY: test

ROGUEC_SRC = $(shell find Source/RogueC | grep .rogue)
ROGUEC_ROGUE_FLAGS =
ROGUEC_CPP_FLAGS = -Wall -std=gnu++11 -fno-strict-aliasing -Wno-invalid-offsetof

DEFAULT_CXX = \"$(CXX) $(ROGUEC_CPP_FLAGS)\"

BINDIR = /usr/local/bin

ifeq ($(OS),Windows_NT)
  SUDO_CMD =
  PLATFORM = Windows
else
  SUDO_CMD = sudo
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PLATFORM = macOS
  else
    PLATFORM = Linux
  endif
endif

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
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec

exhaustive: libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --exhaustive..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC --exhaustive $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec

roguec: bootstrap_roguec $(BINDIR)/roguec libraries Source/RogueC/Build/RogueC.cpp Programs/$(PLATFORM)/roguec

touch_roguec:
	touch Source/RogueC/RogueC.rogue

bootstrap_roguec:
	@if [ ! -f "Programs/$(PLATFORM)/roguec" ]; \
	then \
	  if [ -d "Programs/RogueC" ]; \
	  then \
	    echo "\nRemoving executable from old location Programs/RogueC"; \
	    rm -r Programs/RogueC; \
	  fi; \
	  if [ -f "$(BINDIR)/roguec" ]; \
	  then \
	    echo "\nUnlinking old program location by removing $(BINDIR)/roguec"; \
	    $(SUDO_CMD) rm $(BINDIR)/roguec; \
	  fi; \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/$(PLATFORM)/roguec from C++ source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec; \
	  mkdir -p Programs/$(PLATFORM); \
	  $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec; \
	fi;

Source/RogueC/Build/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC $(ROGUEC_ROGUE_FLAGS)

Programs/$(PLATFORM)/roguec: Source/RogueC/Build/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/$(PLATFORM)/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec

libraries:
	@mkdir -p Programs/$(PLATFORM)
	@if [ $$(rsync -rtv --delete --exclude=.*.sw? --dry-run Source/Libraries Programs/$(PLATFORM) | wc -l) -gt 4 ]; \
	then \
	  echo "==== Updating Libraries ===="; \
		rsync -rtv --delete --exclude=.*.sw? Source/Libraries Programs/$(PLATFORM) | tail -n +2 | (tac 2> /dev/null || tail -r) | tail -n +4 | (tac 2> /dev/null || tail -r); \
	fi

libs: libraries

if:
	echo $(COPY_COUNT)

$(BINDIR)/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating $(BINDIR)/roguec linked to Programs/$(PLATFORM)/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/$(PLATFORM)/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > roguec.script
	@echo Copying roguec.script to $(BINDIR)/roguec
	$(SUDO_CMD) cp roguec.script $(BINDIR)/roguec; \
	$(SUDO_CMD) chmod a+x $(BINDIR)/roguec; \
	rm roguec.script
	@echo
	@echo You can execute the following as a simple test:
	@echo
	@echo "    println '\"Hello World!\"' > Hello.rogue"
	@echo '    roguec Hello.rogue --execute'
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
	@echo "Recompiling RogueC.cpp -> Programs/$(PLATFORM)/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/$(PLATFORM)/roguec

revert_cpp_source:
	git checkout Source/RogueC/Build && rm -f Programs/$(PLATFORM)/roguec

.PHONY: clean
clean:
	rm -rf Programs/$(PLATFORM)
	rm -f Hello.cpp
	rm -f Hello.h
	rm -f ./a.out
	rm -f ./hello

docs:
	cd Source/DocGen && make

