ZOR_SOURCE = $(shell ls *.zor)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc
BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: compile run

compile: ../../Programs/Zorc.bc

../../Programs/Zorc.bc: $(ZOR_SOURCE) $(BARDVM)
	$(BARDVM) $(BCC) $(BCC_ARGS) Zorc.bard

$(BARDVM): $(BCC)
	cd ../../Programs/Bard && make

run:
	$(BARDVM) Zorc.bc

clean:
	rm -f *.bc
