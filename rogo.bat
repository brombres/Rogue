@if not exist ".rogo\Build.exe" (
  if not exist ".rogo" mkdir ".rogo"
  cl Source\RogueC\Bootstrap\Build.cpp /EHsc /Fe.rogo\Build.exe
)
@.rogo\Build.exe check_bootstrap
