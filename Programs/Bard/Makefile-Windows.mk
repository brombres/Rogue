BARDFILES = Projects\BardVM\Bard.c Libraries\Bard\VM\C\Bard.h Libraries\Bard\VM\C\BardAllocator.c Libraries\Bard\VM\C\BardAllocator.h Libraries\Bard\VM\C\BardArray.c Libraries\Bard\VM\C\BardArray.h Libraries\Bard\VM\C\BardBCLoader.c Libraries\Bard\VM\C\BardBCLoader.h Libraries\Bard\VM\C\BardDefines.h Libraries\Bard\VM\C\BardFileReader.c Libraries\Bard\VM\C\BardFileReader.h Libraries\Bard\VM\C\BardList.c Libraries\Bard\VM\C\BardList.h Libraries\Bard\VM\C\BardMM.c Libraries\Bard\VM\C\BardMM.h Libraries\Bard\VM\C\BardMessageManager.c Libraries\Bard\VM\C\BardMessageManager.h Libraries\Bard\VM\C\BardMethod.c Libraries\Bard\VM\C\BardMethod.h Libraries\Bard\VM\C\BardObject.c Libraries\Bard\VM\C\BardObject.h Libraries\Bard\VM\C\BardProperty.c Libraries\Bard\VM\C\BardProperty.h Libraries\Bard\VM\C\BardStandardLibrary.c Libraries\Bard\VM\C\BardStandardLibrary.h Libraries\Bard\VM\C\BardString.c Libraries\Bard\VM\C\BardString.h Libraries\Bard\VM\C\BardType.c Libraries\Bard\VM\C\BardType.h Libraries\Bard\VM\C\BardUtil.c Libraries\Bard\VM\C\BardUtil.h Libraries\Bard\VM\C\BardVM.c Libraries\Bard\VM\C\BardVM.h Libraries\Bard\VM\C\BardOpcodes.c Libraries\Bard\VM\C\BardOpcodes.h Libraries\Bard\VM\C\BardProgram.c Libraries\Bard\VM\C\BardProgram.h 

all: vm

vm: Programs\Windows\bard.exe

Programs\Windows\bard.exe: $(BARDFILES)
	msbuild Projects\BardVM\Windows\BardVM-Windows.sln /p:Configuration=Release

bcc:
	@Programs\Windows\bard.exe Programs\BCC.bc BCC.bard --source-folders=Libraries/Bard --output=Programs\BCC-New.bc
	@copy /y Programs\BCC-New.bc Programs\BCC.bc

btest:
	@Programs\Windows\bard.exe Programs\BCC.bc Test\Test.bard --source-folders=Libraries/Bard
	@Programs\Windows\bard.exe Test\Test.bc
