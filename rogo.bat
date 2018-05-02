@if not exist ".rogo\Build.exe" (
  if not exist ".rogo" mkdir ".rogo"
  cl Source\RogueC\Bootstrap\Build.cpp /EHsc /nologo /Fe.rogo\Build.exe
  @if exist Build.obj ( del Build.obj )
)
@.rogo\Build.exe check_bootstrap
@rogo.exe %*
