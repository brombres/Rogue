.PHONY: cpp

ROGUE_SOURCE = $(shell ls *.rogue)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: compile run

compile: ../../Programs/RogueC.bc

../../Programs/RogueC.bc: $(ROGUE_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) RogueC.bard

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

vm:
	cd ../../Programs/Bard && make

run:
	$(BARDVM) RogueC.bc

list:
	cat CPP/RogueProgram.h
	@echo "-----------------------------------------------------"
	cat CPP/RogueProgram.cpp

cpp: compile run
	cd CPP && make

clean:
	rm -f *.bc
