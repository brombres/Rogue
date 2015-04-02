.PHONY: cpp

ROGUE_SOURCE = $(shell ls *.rogue)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: bcc cpp

bcc: Bard/RogueC.bc

Bard/RogueC.bc: $(ROGUE_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) Bard/RogueC.bard

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

vm:
	cd ../../Programs/Bard && make

compile:
	$(BARDVM) Bard/RogueC.bc

list: go
	cat CPP/RogueProgram.h
	@echo "-----------------------------------------------------"
	cat CPP/RogueProgram.cpp

cpp: compile
	cd CPP && make

clean:
	rm -f Bard/*.bc

vim:
	cd ../../Syntax/Vim && make
