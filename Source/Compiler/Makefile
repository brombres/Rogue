ONO_SOURCE = $(shell ls *.ono)
BARD = ../../Programs/Bard/Programs/Mac/bard
BCC  = ../../Programs/Bard/Programs/BCC.bc --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard

all: go

go: compile run

compile: ../../Programs/ZorCompiler.bc

../../Programs/ZorCompiler.bc: $(ONO_SOURCE)
	$(BARD) $(BCC) ZorCompiler.bard

run:
	$(BARD) ZorCompiler.bc
