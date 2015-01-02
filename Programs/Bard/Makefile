VM_FILES = Projects/BardVM/Bard.c \
	   $(shell find Libraries/Bard/VM/C/*.c) \
	   $(shell find Libraries/Bard/VM/C/*.h) \
	   $(shell find Libraries/Crom/*.c) \
	   $(shell find Libraries/Crom/*.h)

BARD_FILES = $(shell find Libraries/Bard | grep ".*\.bard")

all: vm bcc

clean:
	rm -f Programs/Mac/bard
	rm -f Projects/BardVM/Mac/build

vm:     Programs/Mac/bard

bcc:    Programs/BCC.bc

test:   Test/Test.bc run

cpptest:
	Programs/Mac/bard Programs/BCC.bc Test/Test.bard --source-folders=Libraries/Bard --target=cpp --custom=Libraries/Bard/BCC/Custom.cpp --output=Test.cpp

checktest: Programs/BCC-new.bc Test/CheckTest.bc

run:
	@echo
	@Programs/Mac/bard Test/Test.bc
	@echo

Test/Test.bc: $(VM_FILES) $(BARD_FILES) Test/Test.bard
	Programs/Mac/bard Programs/BCC.bc Test/Test.bard --source-folders=Libraries/Bard

Test/CheckTest.bc: $(VM_FILES) $(BARD_FILES) Test/Test.bard Programs/BCC-new.bc
	Programs/Mac/bard Programs/BCC-new.bc Test/Test.bard --source-folders=Libraries/Bard --output=Test/CheckTest.bc

Programs/Mac/bard: $(VM_FILES)
	chmod a+x BuildVM
	./BuildVM

Programs/BCC.bc: $(BARD_FILES)
	Programs/Mac/bard Programs/BCC.bc BCC.bard --output=Programs/BCC-new.bc --source-folders=Libraries/Bard
	mv Programs/BCC.bc Programs/BCC-old.bc
	mv Programs/BCC-new.bc Programs/BCC.bc

Programs/BCC-new.bc: $(BARD_FILES)
	Programs/Mac/bard Programs/BCC.bc BCC.bard --output=Programs/BCC-new.bc --source-folders=Libraries/Bard

Test/CPPTest.bc: $(BARD_FILES)
	Programs/Mac/bard Programs/BCC.bc BCC.bard --output=Test/CPPTest.bc --source-folders=Libraries/Bard

Test/CPPTest.cpp: Test/Test.bard Test/CPPTest.bc
	Programs/Mac/bard Test/CPPTest.bc Test.bard --output=Test/CPPTest.cpp --source-folders=Libraries/Bard --target=cpp

