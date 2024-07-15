@if not exist Build ( mkdir Build )
cl /O2 /nologo /bigobj Source/RogueC/Bootstrap/RogueC.c /FoBuild\roguec.obj /FeBuild\roguec.exe

@if exist Build\Libraries ( rmdir /S /Q Build\Libraries )
@if not exist Build\Libraries ( mkdir Build\Libraries )
xcopy /I /S /Q /Y Source\Libraries Build\Libraries

@if "%~1" == "build" goto DONE
@echo Add the following to Environment Variables ^> User variables ^> Path:
@echo   %CD%\Build

:DONE
