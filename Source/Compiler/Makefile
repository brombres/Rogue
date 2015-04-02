.PHONY: cpp

ROGUE_SOURCE = $(shell ls *.rogue)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: bcc cpp

bcc: ../../Programs/RogueC.bc

../../Programs/RogueC.bc: $(ROGUE_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) RogueC.bard

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

vm:
	cd ../../Programs/Bard && make

compile:
	$(BARDVM) RogueC.bc

list: go
	cat CPP/RogueProgram.h
	@echo "-----------------------------------------------------"
	cat CPP/RogueProgram.cpp

cpp: compile
	cd CPP && make

clean:
	rm -f *.bc

vim:
	cd ../../Syntax/Vim && make
