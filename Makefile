.PHONY: test

ROGUEC_SRC = $(shell find Source/RogueC | grep .rogue)
ROGUEC_ROGUE_FLAGS = --debug
ROGUEC_CPP_FLAGS = -std=c++11 -fno-strict-aliasing

all: roguec

remake: libraries touch_roguec roguec

debug: libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --debug..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --main --output=Build/RogueC --debug $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

requisite: libraries
	@echo -------------------------------------------------------------------------------
	@echo "Recompiling RogueC.rogue -> RogueC.cpp with --requisite..."
	@echo -------------------------------------------------------------------------------
	cd Source/RogueC && mkdir -p Build
	cd Source/RogueC && roguec RogueC.rogue --main --output=Build/RogueC --requisite $(ROGUEC_ROGUE_FLAGS)
	mkdir -p Programs
	$(CXX) $(ROGUEC_CPP_FLAGS) Source/RogueC/Build/RogueC.cpp -o Programs/RogueC/roguec

roguec: bootstrap_roguec /usr/local/bin /usr/local/bin/roguec libraries Source/RogueC/Build/RogueC.cpp Programs/RogueC/roguec

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
	cd Source/RogueC && roguec RogueC.rogue --main --output=Build/RogueC $(ROGUEC_ROGUE_FLAGS)

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

revert:
	git checkout Source/RogueC/Build && rm -f Programs/RogueC/roguec
	make

clean:
	rm -rf Programs/RogueC
	rm -f /usr/local/bin/roguec
	rm -f Hello.cpp
	rm -f Hello.h
	rm -f ./a.out
	rm -f ./hello

publish:
	git checkout master
	git pull
	git merge develop
	git push
	git checkout develop
	git push
	@[ -f Local.mk ] && make -f Local.mk publish || true

docs:
	cd Source/DocGen && make

# Local.mk
#publish:
	#cd Wiki && git pull
	#cd Source/DocGen && make
	#cd Wiki && git commit -am "Updated documentation" && git push
