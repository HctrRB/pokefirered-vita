# Generates every host-side asset the Vita build consumes (converted graphics, audio samples,
# MIDI songs, map/JSON-derived headers) without a GBA cross compiler. Reuses the decomp's own
# rule files, so the outputs are identical to those of pokefirered's Makefile.
#
# Adapted from pokeemerald-vita's vita/assets.mk (MIT) for pret/pokefirered.
#
# Run from the repository root, after `make -f make_tools.mk`:
#   make -f vita/assets.mk -j8

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

GAME_VERSION  := FIRERED
GAME_REVISION := 1
GAME_LANGUAGE := ENGLISH

TOOLS_DIR := tools
GFX       := $(TOOLS_DIR)/gbagfx/gbagfx$(EXE)
WAV2AGB   := $(TOOLS_DIR)/wav2agb/wav2agb$(EXE)
MID       := $(TOOLS_DIR)/mid2agb/mid2agb$(EXE)
SCANINC   := $(TOOLS_DIR)/scaninc/scaninc$(EXE)
PREPROC   := $(TOOLS_DIR)/preproc/preproc$(EXE)
MAPJSON   := $(TOOLS_DIR)/mapjson/mapjson$(EXE)
JSONPROC  := $(TOOLS_DIR)/jsonproc/jsonproc$(EXE)

C_SUBDIR        := src
DATA_SRC_SUBDIR := src/data
DATA_ASM_SUBDIR := data
SONG_SUBDIR     := sound/songs
MID_SUBDIR      := sound/songs/midi

# The rule files also define object rules keyed on these; point them somewhere unused.
OBJ_DIR            := build/vita-assets
C_BUILDDIR         := $(OBJ_DIR)/$(C_SUBDIR)
DATA_ASM_BUILDDIR  := $(OBJ_DIR)/$(DATA_ASM_SUBDIR)
SONG_BUILDDIR      := $(OBJ_DIR)/$(SONG_SUBDIR)
MID_BUILDDIR       := $(OBJ_DIR)/$(MID_SUBDIR)

SHELL := bash -o pipefail
MAKEFLAGS += --no-print-directory
.SUFFIXES:
.SECONDARY:
.DELETE_ON_ERROR:
.SECONDEXPANSION:

# Must precede the includes, or their first rule becomes the default goal.
.DEFAULT_GOAL := all

AUTO_GEN_TARGETS :=
include graphics_file_rules.mk
include tileset_rules.mk
include map_data_rules.mk
include spritesheet_rules.mk
include json_data_rules.mk
include audio_rules.mk

%.s: ;
%.png: ;
%.pal: ;
%.wav: ;

%.1bpp: %.png  ; $(GFX) $< $@
%.4bpp: %.png  ; $(GFX) $< $@
%.8bpp: %.png  ; $(GFX) $< $@
%.gbapal: %.pal ; $(GFX) $< $@
%.gbapal: %.png ; $(GFX) $< $@
%.lz: % ; $(GFX) $< $@
%.rl: % ; $(GFX) $< $@

# Binary assets are discovered the way the GBA Makefile does it: scaninc follows
# #include / INCBIN / .include / .incbin from every source the port compiles.
C_SRCS    := $(filter-out %.inc.c,$(wildcard $(C_SUBDIR)/*.c $(C_SUBDIR)/*/*.c $(C_SUBDIR)/*/*/*.c))
ASM_SRCS  := $(wildcard $(DATA_ASM_SUBDIR)/*.s $(SONG_SUBDIR)/*.s)
MID_SRCS  := $(wildcard $(MID_SUBDIR)/*.mid)

ASSET_EXTS := %.1bpp %.4bpp %.8bpp %.gbapal %.lz %.rl %.bin %.latfont %.hwlatfont %.fwlatfont %.hwjpnfont %.fwjpnfont

ifeq (,$(filter generated clean-assets,$(MAKECMDGOALS)))
SCANNED := $(shell for f in $(C_SRCS); do $(SCANINC) -I include $$f; done; \
                   for f in $(ASM_SRCS); do $(SCANINC) -I include -I "" $$f; done | tr -d '\r')
endif
BINARY_ASSETS := $(sort $(filter $(ASSET_EXTS),$(SCANNED)))
MID_ASM       := $(MID_SRCS:%.mid=%.s)
MAP_INCS      := $(MAP_CONNECTIONS) $(MAP_EVENTS) $(MAP_HEADERS) \
                 $(MAPS_DIR)/headers.inc $(MAPS_DIR)/groups.inc $(MAPS_DIR)/connections.inc $(MAPS_DIR)/events.inc \
                 $(LAYOUTS_DIR)/layouts.inc $(LAYOUTS_DIR)/layouts_table.inc

.PHONY: all generated assets clean-assets

# Two passes: headers/.inc first, so the binary scan sees a complete include tree.
all:
	@$(MAKE) -f vita/assets.mk generated
	@$(MAKE) -f vita/assets.mk assets

generated: $(AUTO_GEN_TARGETS) $(MAP_INCS)

assets: $(AUTO_GEN_TARGETS) $(MAP_INCS) $(MID_ASM) $(BINARY_ASSETS)
	@echo "vita/assets.mk: $(words $(BINARY_ASSETS)) binary assets, $(words $(MID_ASM)) songs up to date"

clean-assets:
	rm -rf $(OBJ_DIR)
