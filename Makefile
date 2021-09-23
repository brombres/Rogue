INSTALL_FOLDER=/usr/local/bin

ROGUEC_CPP_FLAGS = -Wall -std=gnu++11 -fno-strict-aliasing -Wno-invalid-offsetof

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

BUILD_EXE  = `pwd`/Build/RogueC-$(PLATFORM)
LINK_EXE   = $(INSTALL_FOLDER)/roguec
CPP_SRC    = Source/RogueC/Bootstrap/RogueC.cpp
H_SRC      = Source/RogueC/Bootstrap/RogueC.h

all: $(LINK_EXE)

homebrew: all

$(INSTALL_FOLDER)/roguec: $(CPP_SRC) $(H_SRC)
	mkdir -p Build
	mkdir -p "$(INSTALL_FOLDER)" || sudo mkdir -p "$(INSTALL_FOLDER)"
	$(CXX) -O3 $(ROGUEC_CPP_FLAGS) -DDEFAULT_CXX="\"$(CXX) $(ROGUEC_CPP_FLAGS)\"" $(CPP_SRC) -o "$(BUILD_EXE)"
	rm -f "$(LINK_EXE)" || sudo rm -f "$(LINK_EXE)"
	ln -s "$(BUILD_EXE)" "$(LINK_EXE)" || sudo ln -s "$(BUILD_EXE)" "$(LINK_EXE)"
	cp -r Source/Libraries Build
	@echo ┌─────────────────┐
	@echo │roguec installed!│
	@echo └─────────────────┘

uninstall:
	rm -rf "$(LINK_EXE)" || sudo rm -rf "$(LINK_EXE)"
	rm -rf Build
	@echo ┌──────────────────┐
	@echo │roguec uninstalled│
	@echo └──────────────────┘

