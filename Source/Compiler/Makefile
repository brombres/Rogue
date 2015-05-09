.PHONY: cpp

ROGUE_SOURCE = $(shell ls Bard/*.bard)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: bcc cpp

bcc: Bard/RogueC.bc

Bard/RogueC.bc: $(ROGUE_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) Bard/BardEntry.bard --output=Bard/RogueC.bc

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

vm:
	cd ../../Programs/Bard && make

compile:
	$(BARDVM) Bard/RogueC.bc Test.rogue --output=CPP/RogueProgram.cpp --main

list: go
	cat CPP/RogueProgram.h
	@echo "-----------------------------------------------------"
	cat CPP/RogueProgram.cpp

cpp: compile
	cd CPP && make

clean:
	rm -f Bard/*.bc
	rm -f CPP/RogueProgram.*

vim:
	cd ../../Syntax/Vim && make

roguec: bcc
	$(BARDVM) Bard/RogueC.bc Bard/RogueEntry.rogue --output=CPP/RogueProgram.cpp --main
	cd CPP && make
