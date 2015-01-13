@SET BARDVM=..\..\Programs\Bard\Programs\Windows\bard.exe
@SET BCC=..\..\Programs\Bard\Programs\BCC.bc
@SET ROGUEC=..\..\Programs\Roguec.bc
@SET BCC_ARGS=--source-folders=..\..\Programs\Bard\Libraries\Bard --include=Standard.bard
%BARDVM% %BCC% %BCC_ARGS% Roguec.bard
%BARDVM% Roguec.bc
type Test.cpp

@REM ROGUE_SOURCE = $(shell ls *.rogue)
@REM BARDVM = ../../Programs/Bard/Programs/Mac/bard
@REM BCC    = ../../Programs/Bard/Programs/BCC.bc
@REM BCC_ARGS = --source-folders=../../Programs/Bard/Libraries/Bard --include=Standard.bard
@REM 
@REM all: go
@REM 
@REM go: compile run
@REM 
@REM compile: ../../Programs/Roguec.bc
@REM 
@REM ../../Programs/Roguec.bc: $(ROGUE_SOURCE) $(BARDVM)
@REM 	$(BARDVM) $(BCC) $(BCC_ARGS) Roguec.bard
@REM 
@REM $(BARDVM): $(BCC)
@REM 	cd ../../Programs/Bard && make
@REM 
@REM run:
@REM 	$(BARDVM) Roguec.bc
@REM 	cat Test.cpp
@REM 
@REM cpp: compile run
@REM 	g++ Test.cpp
@REM 	./a.out
@REM 
@REM clean:
@REM 	rm -f *.bc
