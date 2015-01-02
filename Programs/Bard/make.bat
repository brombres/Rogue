@REM make.bat - Windows NMAKE launcher for Bard
@REM 
@echo Be sure that vcvars32.bat has been executed - for example by typing the 
@echo following (your path may very):
@echo. 
@echo   "c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\vcvars32.bat"
@echo. 
@echo Options:
@echo   make vm
@echo   make bcc
nmake /f Makefile-Windows.mk %1
