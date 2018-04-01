.PHONY: test

ROGUEC_SRC = $(shell find Source/RogueC | grep ".rogue$$" )
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
	rogo version
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC --debug $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec

exhaustive: roguec
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --api=* --threads"
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	rogo version
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC --api=* $(ROGUEC_ROGUE_FLAGS) --define=NO_LIBFFI --threads
	mkdir -p Programs
	$(CXX) -pthread $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec

roguec: bootstrap_rogo $(BINDIR)/rogo bootstrap_roguec $(BINDIR)/roguec libraries Source/RogueC/Version.rogue Source/RogueC/Build/RogueC.cpp Programs/RogueC/$(PLATFORM)/roguec

update_bootstrap: rogo
	mkdir -p Source/RogueC/Bootstrap
	cp Source/RogueC/Build/RogueC.h Source/RogueC/Build/RogueC.cpp Source/RogueC/Bootstrap
	cp Source/RogueC/Build/Rogo.h Source/RogueC/Build/Rogo.cpp Source/RogueC/Bootstrap

touch_roguec:
	touch Source/RogueC/RogueC.rogue

bootstrap:
	@echo -------------------------------------------------------------------------------
	@echo Recompiling Programs/RogueC/$(PLATFORM)/roguec from C++ bootstrap source...
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs/RogueC/$(PLATFORM);
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec
	touch Source/RogueC/RogueC.rogue
	@echo -------------------------------------------------------------------------------
	@echo Recompiling Programs/RogueC/$(PLATFORM)/rogo from C++ bootstrap source...
	@echo -------------------------------------------------------------------------------
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/Rogo.cpp -o Programs/RogueC/$(PLATFORM)/rogo
	touch Source/Tools/Rogo/Rogo.rogue


bootstrap_roguec:
	@if [ ! -f "Programs/RogueC/$(PLATFORM)/roguec" ]; \
	then \
	  if [ -f "$(BINDIR)/roguec" ]; \
	  then \
	    echo "\nUnlinking old program location by removing $(BINDIR)/roguec"; \
	    $(SUDO_CMD) rm $(BINDIR)/roguec; \
	  fi; \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/RogueC/$(PLATFORM)/roguec from C++ bootstrap source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec; \
	  mkdir -p Programs/RogueC/$(PLATFORM); \
	  $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec; \
	  echo touch Source/RogueC/RogueC.rogue; \
	  touch Source/RogueC/RogueC.rogue; \
	fi;

version:
	rogo version

Source/RogueC/Version.rogue:
	rogo version

Source/RogueC/Build/RogueC.cpp: $(ROGUEC_SRC)
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	rogo version
	cd Source/RogueC && roguec RogueC.rogue --gc=manual --main --output=Build/RogueC $(ROGUEC_ROGUE_FLAGS)

Programs/RogueC/$(PLATFORM)/roguec: Source/RogueC/Build/RogueC.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.cpp -> Programs/RogueC/$(PLATFORM)/roguec..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/$(PLATFORM)/roguec

bootstrap_rogo:
	@if [ ! -f "Programs/RogueC/$(PLATFORM)/rogo" ]; \
	then \
	  if [ -f "$(BINDIR)/rogo" ]; \
	  then \
	    echo "\nUnlinking old program location by removing $(BINDIR)/rogo"; \
	    $(SUDO_CMD) rm $(BINDIR)/rogo; \
	  fi; \
	  echo -------------------------------------------------------------------------------; \
	  echo Compiling Programs/RogueC/$(PLATFORM)/rogo from C++ bootstrap source...; \
	  echo -------------------------------------------------------------------------------; \
	  echo $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/Rogo.cpp -o Programs/RogueC/$(PLATFORM)/rogo; \
	  mkdir -p Programs/RogueC/$(PLATFORM); \
	  $(CXX) $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Bootstrap/Rogo.cpp -o Programs/RogueC/$(PLATFORM)/rogo; \
	  echo touch Source/Tools/Rogo/Rogo.rogue; \
	  touch Source/Tools/Rogo/Rogo.rogue; \
	fi;

rogo: Source/RogueC/Build/Rogo.cpp Programs/RogueC/$(PLATFORM)/rogo $(BINDIR)/rogo


Programs/RogueC/$(PLATFORM)/rogo: Source/RogueC/Build/Rogo.cpp
	@echo -------------------------------------------------------------------------------
	@echo "Compiling Rogo.cpp -> Programs/RogueC/$(PLATFORM)/rogo..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Programs
	$(CXX) -pthread $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="$(DEFAULT_CXX)" Source/RogueC/Build/Rogo.cpp -o Programs/RogueC/$(PLATFORM)/rogo

Source/RogueC/Build/Rogo.cpp: $(ROGUEC_SRC) Source/Tools/Rogo/Rogo.rogue
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling Rogo.rogue -> Rogo.cpp..."
	@echo -------------------------------------------------------------------------------
	mkdir -p Source/RogueC/Build
	roguec Source/Tools/Rogo/Rogo.rogue --gc=manual --main --output=Source/RogueC/Build/Rogo $(ROGUEC_ROGUE_FLAGS)

libraries:
	@mkdir -p Programs/RogueC/$(PLATFORM)
	@if [ -z "$(IGNORE_LIBS)" -a $$(rsync -rtvk --exclude=".*" --exclude=".*/" --delete --exclude=.*.sw? --dry-run Source/Libraries Programs/RogueC/$(PLATFORM) | wc -l) -gt 4 ]; \
	then \
	  echo "==== Updating Libraries ===="; \
		rsync -rtvk --delete --exclude=".*" --exclude=".*/" Source/Libraries Programs/RogueC/$(PLATFORM) | tail -n +2 | (tac 2> /dev/null || tail -r) | tail -n +4 | (tac 2> /dev/null || tail -r); \
	fi

libs: libraries

if:
	echo $(COPY_COUNT)

$(BINDIR)/roguec:
	@echo -------------------------------------------------------------------------------
	@echo Creating $(BINDIR)/roguec linked to Programs/RogueC/$(PLATFORM)/roguec
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/RogueC/$(PLATFORM)/roguec\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > roguec.script
	@echo Copying roguec.script to $(BINDIR)/roguec
	$(SUDO_CMD) cp roguec.script $(BINDIR)/roguec; \
	$(SUDO_CMD) chmod a+x $(BINDIR)/roguec; \
	rm roguec.script
	@echo
	@echo You can execute the following as a simple test:
	@echo
	@echo "    echo println '\"Hello World!\"' > Hello.rogue"
	@echo '    roguec Hello.rogue --execute'
	@echo

$(BINDIR)/rogo:
	@echo -------------------------------------------------------------------------------
	@echo Creating $(BINDIR)/rogo linked to Programs/RogueC/$(PLATFORM)/rogo
	@echo -------------------------------------------------------------------------------
	printf "%s\nexec \"%s/Programs/RogueC/$(PLATFORM)/rogo\" \"%c@\"\n" '#!/bin/sh' `pwd` '$$' > rogo.script
	@echo Copying rogo.script to $(BINDIR)/rogo
	$(SUDO_CMD) cp rogo.script $(BINDIR)/rogo; \
	$(SUDO_CMD) chmod a+x $(BINDIR)/rogo; \
	rm rogo.script

test:
	rogo test

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

revert: revert_cpp_source libraries bootstrap

revert_cpp_source:
	git checkout Source/RogueC/Bootstrap && rm -f Programs/RogueC/$(PLATFORM)/roguec

.PHONY: clean
clean:
	rm -rf Programs/RogueC/$(PLATFORM)
	rm -rf Source/RogueC/Build
	rm -f Hello.cpp
	rm -f Hello.h
	rm -f ./a.out
	rm -f ./hello

unlink:
	@if [ -f "$$(which roguec)" ]; \
	then \
	  echo "Unlinking roguec by removing $$(which roguec)'"; \
	  $(SUDO_CMD) rm -f $$(which roguec); \
	  $(SUDO_CMD) rm -f $(BINDIR)/roguec; \
	else \
	  if [ -f "$(BINDIR)/roguec" ]; \
	  then \
	    echo "Unlinking roguec by removing $(BINDIR)/roguec'"; \
	    $(SUDO_CMD) rm -f $(BINDIR)/roguec; \
	  else \
	    echo "No roguec to unlink" ; \
	  fi \
	fi; \
	\
	if [ -f "$$(which rogo)" ]; \
	then \
	  echo "Unlinking rogo by removing $$(which rogo)'"; \
	  $(SUDO_CMD) rm -f $$(which rogo); \
	  $(SUDO_CMD) rm -f $(BINDIR)/rogo; \
	else \
	  if [ -f "$(BINDIR)/rogo" ]; \
	  then \
	    echo "Unlinking rogo by removing $(BINDIR)/rogo'"; \
	    $(SUDO_CMD) rm -f $(BINDIR)/rogo; \
	  else \
	    echo "No rogo to unlink" ; \
	  fi \
	fi

docs:
	cd Source/DocGen && make

