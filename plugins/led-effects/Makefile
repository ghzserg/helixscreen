# LED Effects Plugin Makefile
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 HelixScreen

PLUGIN_ID := led-effects
PLUGIN_LIB := libhelix_$(subst -,_,$(PLUGIN_ID))

# HelixScreen root (plugins/ is at HELIX_ROOT/plugins/)
HELIX_ROOT := ../..

# Compiler settings
CXX := clang++
CXXFLAGS := -std=c++17 -fPIC -Wall -Wextra -Wno-unused-parameter
CXXFLAGS += -I$(HELIX_ROOT)/include
CXXFLAGS += -I$(HELIX_ROOT)/lib/lvgl
CXXFLAGS += -I$(HELIX_ROOT)/lib
CXXFLAGS += -I$(HELIX_ROOT)/lib/spdlog/include
CXXFLAGS += -I$(HELIX_ROOT)/lib/libhv/include
CXXFLAGS += -I$(HELIX_ROOT)/lib/libhv/include/hv

# Build type (default: release)
BUILD_TYPE ?= release
ifeq ($(BUILD_TYPE),debug)
    CXXFLAGS += -g -O0 -DDEBUG
else
    CXXFLAGS += -O2 -DNDEBUG
endif

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_EXT := dylib
    LDFLAGS := -shared -undefined dynamic_lookup
else
    SHARED_EXT := so
    LDFLAGS := -shared
endif

# Source files
SRCS := src/led_effects_plugin.cpp
OBJS := $(SRCS:.cpp=.o)

# Output - library goes in plugin root for PluginManager discovery
BUILD_DIR := build
OUTPUT := $(PLUGIN_LIB).$(SHARED_EXT)

.PHONY: all clean install info

all: $(OUTPUT)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OUTPUT): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^
	@echo "Built plugin: $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(OBJS)

# Install plugin to HelixScreen plugins directory (for development)
install: $(OUTPUT)
	@echo "Plugin ready at: $(OUTPUT)"
	@echo "XML component at: ui_xml/led_widget.xml"

info:
	@echo "Plugin ID: $(PLUGIN_ID)"
	@echo "Library: $(OUTPUT)"
	@echo "Platform: $(UNAME_S)"
	@echo "Build type: $(BUILD_TYPE)"
