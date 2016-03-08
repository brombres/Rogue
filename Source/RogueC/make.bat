@SET BARDVM=..\..\Programs\Bard\Programs\Windows\bard.exe
@SET BCC=..\..\Programs\Bard\Programs\BCC.bc
@SET ROGUEC=..\..\Programs\RogueC.bc
@SET BCC_ARGS=--source-folders=..\..\Programs\Bard\Libraries\Bard --include=Standard.bard
%BARDVM% %BCC% %BCC_ARGS% RogueC.bard
@if errorlevel 1 goto done
%BARDVM% RogueC.bc
type Test.cpp
:done

@REM ROGUE_SOURCE = $(shell ls *.rogue)
@REM BARDVM = ../../Programs/Bard/Programs/Mac/bard
@REM BCC    = ../../Programs/Bard/Programs/BCC.bc
@REM BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard
@REM
@REM all: go
@REM
@REM go: compile run
@REM
@REM compile: ../../Programs/RogueC.bc
@REM
@REM ../../Programs/RogueC.bc: $(ROGUE_SOURCE) $(BARDVM)
@REM 	$(BARDVM) $(BCC) $(BCC_ARGS) RogueC.bard
@REM
@REM $(BARDVM): $(BCC)
@REM 	cd ../../Programs/Bard && make
@REM
@REM run:
@REM 	$(BARDVM) RogueC.bc
@REM 	cat Test.cpp
@REM
@REM cpp: compile run
@REM 	g++ Test.cpp
@REM 	./a.out
@REM
@REM clean:
@REM 	rm -f *.bc
