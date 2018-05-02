@if not exist ".rogo\Build.exe" (
  if not exist ".rogo" mkdir ".rogo"
  cl Source\RogueC\Bootstrap\Build.cpp /EHsc /nologo /Fe.rogo\Build.exe
  @if exist Build.obj ( del Build.obj )
)
@if "%1" == "rogo" (
  if exist "Programs\RogueC\rogo.exe" (del Programs\RogueC\rogo.exe)
  @.rogo\Build.exe check_bootstrap
) else (
  @.rogo\Build.exe check_bootstrap
  @rogo.exe %*
)

