@if not exist Build mkdir Build

cl /Ob2ity /GF /Gy /EHsc /nologo Source\RogueC\Bootstrap\RogueC.c /FoBuild\r2.obj /FeBuild\r2.exe
xcopy /I /S /Q /Y Source\Libraries Build\Libraries

@if "%~1" == "build" goto DONE
@echo Add the following to Environment Variables ^> User variables ^> Path:
@echo   %CD%\Build

:DONE
