@REM Reset ERRORLEVEL
@cmd /c "exit 0"

@if "%1" == "bootstrap" (
  if exist ".rogo\Bootstrap.exe" (del ".rogo\Bootstrap.exe")
  if exist ".rogo\Build.exe" (del ".rogo\Build.exe")
  if exist "Programs\RogueC\roguec.exe" (del "Programs\RogueC\roguec.exe")
  if exist "Programs\RogueC\rogo.exe" (del "Programs\RogueC\rogo.exe")
) else (
  @echo > nul
)
@if not exist ".rogo\Bootstrap.exe" (
  if not exist ".rogo" mkdir ".rogo"
  cl /Ob2ity /GF /Gy Source\RogueC\Bootstrap\Build.cpp /EHsc /nologo /Fe.rogo\Bootstrap.exe
  @if exist Build.obj ( del Build.obj )
  @.rogo\Bootstrap.exe check_bootstrap
)
@if ERRORLEVEL 1 goto done
@if "%1" == "rogo" (
  if exist "Programs\RogueC\rogo.exe" (del Programs\RogueC\rogo.exe)
  @.rogo\Bootstrap.exe check_bootstrap
) else (
  @.rogo\Bootstrap.exe check_bootstrap
  @if ERRORLEVEL 1 goto done
  @rogo.exe %*
)
:done

