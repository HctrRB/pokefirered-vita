# Builds host-side decompilation asset and preprocessing tools in tools/.
#
# Run from the repository root in MSYS2 MINGW64:
#   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"

MAKEFLAGS += --no-print-directory

TOOLS_DIR := tools
TOOL_NAMES := bin2c gbafix gbagfx jsonproc mapjson mid2agb preproc ramscrgen rsfont scaninc wav2agb

TOOLDIRS := $(TOOL_NAMES:%=$(TOOLS_DIR)/%)

RULES_NO_SCAN += tools check-tools clean-tools $(TOOLDIRS)
.PHONY: $(RULES_NO_SCAN)

tools: $(TOOLDIRS)

$(TOOLDIRS):
	@$(MAKE) -C $@

clean-tools:
	@$(foreach tooldir,$(TOOLDIRS),$(MAKE) clean -C $(tooldir);)
