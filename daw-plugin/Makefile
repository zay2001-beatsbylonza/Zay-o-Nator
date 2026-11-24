#!/usr/bin/make -f
# ZaY-o-Nator for your DAW: VST3 and AU (macOS), VST3 (Windows, Linux). By BeatsbyLonza.
# Needs DPF (DISTRHO Plugin Framework) in ../dpf, or DPF_PATH=<path>:
#   git clone --recursive https://github.com/DISTRHO/DPF ../dpf
#   make                         # this computer's processor
#   make macos-universal-10.15   # Apple Silicon and Intel in one
#   make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++-posix   # Windows VST3, from Linux

NAME = ZaY-o-Nator
DPF_PATH ?= ../dpf
DPF_TARGET_DIR = $(CURDIR)/bin
DPF_BUILD_DIR = $(CURDIR)/build

FILES_DSP = ZayONatorPlugin.cpp
FILES_UI  = ZayONatorUI.cpp

include $(DPF_PATH)/Makefile.plugins.mk

# The title font (DejaVu Serif Italic, see fonts/DejaVu-LICENSE.txt), built into the plugin
FontSerifItalic.hpp: fonts/DejaVuSerif-Italic.ttf
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); \
open(sys.argv[2],'w').write('// generated from %s\n#pragma once\nstatic const unsigned char kFontSerifItalic[] = {%s};\nstatic const unsigned int kFontSerifItalicSize = %d;\n' % (sys.argv[1], ','.join(str(b) for b in d), len(d)))" $< $@

$(BUILD_DIR)/ZayONatorUI.cpp.o: FontSerifItalic.hpp

# Windows: the meters go out over Winsock
ifeq ($(WINDOWS),true)
LINK_FLAGS += -lws2_32
endif

TARGETS = vst3
ifeq ($(MACOS),true)
TARGETS += au
endif

all: $(TARGETS)
