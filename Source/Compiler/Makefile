.PHONY: cpp

ROGUE_SOURCE = $(shell ls *.rogue)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: compile run

compile: ../../Programs/Roguec.bc

../../Programs/Roguec.bc: $(ROGUE_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) Roguec.bard

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

run:
	$(BARDVM) Roguec.bc
	cat CPP/Test.h
	@echo "-----------------------------------------------------"
	cat CPP/Test.cpp

#cpp: compile run
#g++ Test.cpp
#./a.out

cpp:
	cd CPP && make

clean:
	rm -f *.bc
