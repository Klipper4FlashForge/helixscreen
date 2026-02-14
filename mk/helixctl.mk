# Copyright (c) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen - helixctl remote control tool
# Standalone CLI for controlling a running HelixScreen instance via Unix socket

# ==============================================================================
# helixctl Tool
# ==============================================================================
# Standalone binary - only needs POSIX sockets + nlohmann/json
# No LVGL, no libhv linking, no SDL2
# Usage: helixctl [options] <command> [args...]

HELIXCTL_SRC := $(TOOLS_DIR)/helixctl.cpp
HELIXCTL_OBJ := $(OBJ_DIR)/tools/helixctl.o
HELIXCTL_BIN := $(BIN_DIR)/helixctl

# Linenoise (bundled line-editing library for REPL)
LINENOISE_SRC := lib/linenoise/linenoise.c
LINENOISE_OBJ := $(OBJ_DIR)/tools/linenoise.o

# Minimal include paths - json.hpp from libhv + linenoise
HELIXCTL_INCLUDES := -isystem lib/libhv/include -I lib/linenoise

# No special link dependencies - just standard C++ library
HELIXCTL_LDFLAGS := -lpthread

# Build rule for helixctl
$(HELIXCTL_BIN): $(HELIXCTL_OBJ) $(LINENOISE_OBJ)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)$(BOLD)[LD]$(RESET) $@"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(HELIXCTL_LDFLAGS) || { \
		echo "$(RED)$(BOLD)✗ Linking failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)✓ helixctl built: $@$(RESET)"

# Compile helixctl source
$(HELIXCTL_OBJ): $(HELIXCTL_SRC)
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[CXX]$(RESET) $<"
ifeq ($(V),1)
	$(Q)echo "$(YELLOW)Command:$(RESET) $(CXX) $(CXXFLAGS) $(HELIXCTL_INCLUDES) -c $< -o $@"
endif
	$(Q)$(CXX) $(CXXFLAGS) $(HELIXCTL_INCLUDES) -c $< -o $@ || { \
		echo "$(RED)$(BOLD)✗ Compilation failed:$(RESET) $<"; \
		exit 1; \
	}

# Compile linenoise (C, not C++)
$(LINENOISE_OBJ): $(LINENOISE_SRC)
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[CC]$(RESET) $<"
	$(Q)$(CC) -std=c99 -Wall -Os -c $< -o $@ || { \
		echo "$(RED)$(BOLD)✗ Compilation failed:$(RESET) $<"; \
		exit 1; \
	}

# Phony target
.PHONY: helixctl

helixctl: $(HELIXCTL_BIN)
	$(ECHO) "$(CYAN)Usage: $(YELLOW)./$(HELIXCTL_BIN) [--socket <path>] <command> [args...]$(RESET)"
	$(ECHO) "$(CYAN)Commands: $(YELLOW)ping, navigate, go_back, list_panels, current, screenshot, status$(RESET)"
