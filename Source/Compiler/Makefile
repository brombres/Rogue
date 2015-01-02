ZOR_SOURCE = $(shell ls *.zor)
BARDVM = ../../Programs/Bard/Programs/Mac/bard
BCC    = ../../Programs/Bard/Programs/BCC.bc --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: compile run

compile: ../../Programs/Zorcom.bc

../../Programs/Zorcom.bc: $(ZOR_SOURCE)
	$(BARDVM) $(BCC) Zorcom.bard

run:
	$(BARDVM) Zorcom.bc

clean:
	rm -f *.bc
