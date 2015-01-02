SOURCE       = ../../Libraries/Bard/VM/C
INTERMEDIATE = ../../Build/Intermediate/BardVM/C
PRODUCTS     = ../../Build/Products/BardVM/C

HEADERS = $(SOURCE)/Bard.h \
	  $(SOURCE)/BardVMDefines.h \
	  $(SOURCE)/BardVMUtil.h \
	  $(SOURCE)/BardVMAllocator.h \
	  $(SOURCE)/BardVMObject.h \
	  $(SOURCE)/BardVMString.h \
	  $(SOURCE)/BardVMReader.h \
	  $(SOURCE)/BardVMLoader.h \
	  $(SOURCE)/BardVMType.h \
	  $(SOURCE)/BardVMProperty.h \
	  $(SOURCE)/BardVMMethod.h \
	  $(SOURCE)/BardMM.h \
	  $(SOURCE)/BardVM.h \
	  $(SOURCE)/BardVMProcessor.h \
	  $(SOURCE)/BardVMOpcodes.h \
	  $(SOURCE)/BardVMMessageManager.h \
	  $(SOURCE)/BardVMStandardLibrary.h \
	  $(SOURCE)/BardVMArray.h \
	  $(SOURCE)/BardVMList.h

OBJECTS = $(INTERMEDIATE)/Bard.o \
	  $(INTERMEDIATE)/BardVMUtil.o \
	  $(INTERMEDIATE)/BardVMAllocator.o \
	  $(INTERMEDIATE)/BardVMObject.o \
	  $(INTERMEDIATE)/BardVMString.o \
	  $(INTERMEDIATE)/BardVMReader.o \
	  $(INTERMEDIATE)/BardVMLoader.o \
	  $(INTERMEDIATE)/BardVMType.o \
	  $(INTERMEDIATE)/BardVMProperty.o \
	  $(INTERMEDIATE)/BardVMMethod.o \
	  $(INTERMEDIATE)/BardMM.o \
	  $(INTERMEDIATE)/BardVM.o \
	  $(INTERMEDIATE)/BardVMProcessor.o \
	  $(INTERMEDIATE)/BardVMOpcodes.o \
	  $(INTERMEDIATE)/BardVMMessageManager.o \
	  $(INTERMEDIATE)/BardVMStandardLibrary.o \
	  $(INTERMEDIATE)/BardVMArray.o \
	  $(INTERMEDIATE)/BardVMList.o
FLAGS   = -Wall -O3 -fno-strict-aliasing -I $(SOURCE)

#all: create_folders $(PRODUCTS)/B4

all:
	@echo TODO old makefile, needs overhaul

$(PRODUCTS)/B4: $(HEADERS) $(OBJECTS)
	gcc $(FLAGS) $(OBJECTS) -o $(PRODUCTS)/B4
	chmod a+x $(PRODUCTS)/B4

create_folders:
	@mkdir -p $(INTERMEDIATE)
	@mkdir -p $(PRODUCTS)

clean:
	rm -f $(INTERMEDIATE)/*.o
	rm -f $(PRODUCTS)/B4

$(INTERMEDIATE)/Bard.o: $(HEADERS) Bard.c
	gcc -c $(FLAGS) Bard.c -o $(INTERMEDIATE)/Bard.o

$(INTERMEDIATE)/BardVMUtil.o: $(HEADERS) $(SOURCE)/BardVMUtil.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMUtil.c -o $(INTERMEDIATE)/BardVMUtil.o

$(INTERMEDIATE)/BardVMAllocator.o: $(HEADERS) $(SOURCE)/BardVMAllocator.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMAllocator.c -o $(INTERMEDIATE)/BardVMAllocator.o

$(INTERMEDIATE)/BardVMObject.o: $(HEADERS) $(SOURCE)/BardVMObject.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMObject.c -o $(INTERMEDIATE)/BardVMObject.o

$(INTERMEDIATE)/BardVMString.o: $(HEADERS) $(SOURCE)/BardVMString.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMString.c -o $(INTERMEDIATE)/BardVMString.o

$(INTERMEDIATE)/BardVMReader.o: $(HEADERS) $(SOURCE)/BardVMReader.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMReader.c -o $(INTERMEDIATE)/BardVMReader.o

$(INTERMEDIATE)/BardVMLoader.o: $(HEADERS) $(SOURCE)/BardVMLoader.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMLoader.c -o $(INTERMEDIATE)/BardVMLoader.o

$(INTERMEDIATE)/BardVMType.o: $(HEADERS) $(SOURCE)/BardVMType.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMType.c -o $(INTERMEDIATE)/BardVMType.o

$(INTERMEDIATE)/BardVMProperty.o: $(HEADERS) $(SOURCE)/BardVMProperty.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMProperty.c -o $(INTERMEDIATE)/BardVMProperty.o

$(INTERMEDIATE)/BardVMMethod.o: $(HEADERS) $(SOURCE)/BardVMMethod.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMMethod.c -o $(INTERMEDIATE)/BardVMMethod.o

$(INTERMEDIATE)/BardMM.o: $(HEADERS) $(SOURCE)/BardMM.c
	gcc -c $(FLAGS) $(SOURCE)/BardMM.c -o $(INTERMEDIATE)/BardMM.o

$(INTERMEDIATE)/BardVM.o: $(HEADERS) $(SOURCE)/BardVM.c $(SOURCE)/BardVMOpcodes.h
	gcc -c $(FLAGS) $(SOURCE)/BardVM.c -o $(INTERMEDIATE)/BardVM.o

$(INTERMEDIATE)/BardVMProcessor.o: $(HEADERS) $(SOURCE)/BardVMProcessor.c $(SOURCE)/BardVMOpcodes.h
	gcc -c $(FLAGS) $(SOURCE)/BardVMProcessor.c -o $(INTERMEDIATE)/BardVMProcessor.o

$(INTERMEDIATE)/BardVMOpcodes.o: $(HEADERS) $(SOURCE)/BardVMOpcodes.c $(SOURCE)/BardVMOpcodes.h
	gcc -c $(FLAGS) $(SOURCE)/BardVMOpcodes.c -o $(INTERMEDIATE)/BardVMOpcodes.o

$(INTERMEDIATE)/BardVMMessageManager.o: $(HEADERS) $(SOURCE)/BardVMMessageManager.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMMessageManager.c -o $(INTERMEDIATE)/BardVMMessageManager.o

$(INTERMEDIATE)/BardVMStandardLibrary.o: $(HEADERS) $(SOURCE)/BardVMStandardLibrary.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMStandardLibrary.c -o $(INTERMEDIATE)/BardVMStandardLibrary.o

$(INTERMEDIATE)/BardVMArray.o: $(HEADERS) $(SOURCE)/BardVMArray.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMArray.c -o $(INTERMEDIATE)/BardVMArray.o

$(INTERMEDIATE)/BardVMList.o: $(HEADERS) $(SOURCE)/BardVMList.c
	gcc -c $(FLAGS) $(SOURCE)/BardVMList.c -o $(INTERMEDIATE)/BardVMList.o

