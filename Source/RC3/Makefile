ROGUE_FILES = $(shell find . | grep .rogue | grep -v Test.rogue)

all: compile run

compile: rc3

run:
	./rc3 Test.rogue

rc3: $(ROGUE_FILES)
	roguec RogueC.rogue --main --output=rc3
	g++ rc3.cpp -o rc3

