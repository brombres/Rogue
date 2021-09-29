.PHONY: build

BUILD_FOLDER=Build
LIBRARIES_FOLDER=$(BUILD_FOLDER)
LINK_EXE=/usr/local/bin/roguec

ROGUEC_CPP_FLAGS = -Wall -std=gnu++11 -fno-strict-aliasing -Wno-invalid-offsetof -O3

ifeq ($(OS),Windows_NT)
  ifneq (,$(findstring /cygdrive/,$(PATH)))
      PLATFORM = Cygwin
  else
      PLATFORM = Windows
  endif
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PLATFORM = macOS
  else
    PLATFORM = Linux
  endif
endif

ABS_BUILD_FOLDER = `(cd $(BUILD_FOLDER) && pwd)`
BUILD_EXE  = RogueC-$(PLATFORM)
CPP_SRC    = Source/RogueC/Bootstrap/RogueC.cpp
H_SRC      = Source/RogueC/Bootstrap/RogueC.h

all: link
	@echo ┌─────────────────┐
	@echo │roguec installed!│
	@echo └─────────────────┘

build: $(BUILD_FOLDER)/$(BUILD_EXE)

$(BUILD_FOLDER)/$(BUILD_EXE): $(CPP_SRC) $(H_SRC)
	mkdir -p "$(BUILD_FOLDER)" || (echo Retrying with sudo && sudo mkdir -p "$(BUILD_FOLDER)")
	$(CXX) $(ROGUEC_CPP_FLAGS) $(CPP_SRC) -o "$(BUILD_FOLDER)/$(BUILD_EXE)"
	cp -r Source/Libraries "$(LIBRARIES_FOLDER)"

link: $(LINK_EXE)

$(LINK_EXE): build
	rm -f "$(LINK_EXE)" || (echo Retrying with sudo && sudo rm -f "$(LINK_EXE)")
	ln -s "$(ABS_BUILD_FOLDER)/$(BUILD_EXE)" "$(LINK_EXE)" \
	 	|| (echo Retrying with sudo && sudo ln -s "$(ABS_BUILD_FOLDER)/$(BUILD_EXE)" "$(LINK_EXE)")

uninstall:
	rm -rf "$(LINK_EXE)" || (echo Retrying with sudo && sudo rm -rf "$(LINK_EXE)")
	rm -rf "$(BUILD_FOLDER)"
	@echo ┌──────────────────┐
	@echo │roguec uninstalled│
	@echo └──────────────────┘

