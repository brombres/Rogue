@if not exist Build ( mkdir Build )
cl /O2 /nologo /bigobj Source/RogueC/Bootstrap/RogueC-1.c Source/RogueC/Bootstrap/RogueC-2.c Source/RogueC/Bootstrap/RogueC-3.c Source/RogueC/Bootstrap/RogueC-4.c Source/RogueC/Bootstrap/RogueC-5.c Source/RogueC/Bootstrap/RogueC-6.c Source/RogueC/Bootstrap/RogueC-7.c Source/RogueC/Bootstrap/RogueC-8.c Source/RogueC/Bootstrap/RogueC-9.c Source/RogueC/Bootstrap/RogueC-10.c Source/RogueC/Bootstrap/RogueC-11.c /FoBuild\roguec.obj /FeBuild\roguec.exe

@if exist Build\Libraries ( rmdir /S /Q Build\Libraries )
@if not exist Build\Libraries ( mkdir Build\Libraries )
xcopy /I /S /Q /Y Source\Libraries Build\Libraries

@if "%~1" == "build" goto DONE
@echo Add the following to Environment Variables ^> User variables ^> Path:
@echo   %CD%\Build

:DONE
