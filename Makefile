# GuppyScreen UI Prototype Makefile
# LVGL 9 + SDL2 simulator

# Compilers
CC := clang
CXX := clang++
CFLAGS := -std=c11 -Wall -Wextra -O2 -g
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj

# LVGL
LVGL_DIR := lvgl
LVGL_INC := -I$(LVGL_DIR) -I$(LVGL_DIR)/src
LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name "*.c")
LVGL_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_SRCS))

# LVGL Demos (separate target)
LVGL_DEMO_SRCS := $(shell find $(LVGL_DIR)/demos -name "*.c")
LVGL_DEMO_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_DEMO_SRCS))

# Application
APP_SRCS := $(wildcard $(SRC_DIR)/*.cpp)
APP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))

# Fonts
FONT_SRCS := assets/fonts/fa_icons_64.c assets/fonts/fa_icons_48.c assets/fonts/fa_icons_32.c
FONT_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(FONT_SRCS))

# SDL2
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS := $(shell sdl2-config --libs)

# Include paths
INCLUDES := -I. -I$(INC_DIR) $(LVGL_INC) $(SDL2_CFLAGS)

# Linker flags
LDFLAGS := $(SDL2_LIBS) -lm -lpthread

# Binary
TARGET := $(BIN_DIR)/guppy-ui-proto

# LVGL configuration
LV_CONF := -DLV_CONF_INCLUDE_SIMPLE

# Parallel build
NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Test configuration
TEST_DIR := tests
TEST_UNIT_DIR := $(TEST_DIR)/unit
TEST_FRAMEWORK_DIR := $(TEST_DIR)/framework
TEST_BIN := $(BIN_DIR)/run_tests
TEST_SRCS := $(wildcard $(TEST_UNIT_DIR)/*.cpp)
TEST_OBJS := $(patsubst $(TEST_UNIT_DIR)/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_SRCS))

.PHONY: all clean run test

all: $(TARGET)

# Link binary
$(TARGET): $(APP_OBJS) $(LVGL_OBJS) $(FONT_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile app sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile LVGL sources
$(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL: $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile font sources
$(OBJ_DIR)/assets/fonts/%.o: assets/fonts/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling font: $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Run the prototype
run: $(TARGET)
	@echo "Running UI prototype..."
	@$(TARGET)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Parallel build target
build:
	@echo "Building with $(NPROC) parallel jobs..."
	@$(MAKE) -j$(NPROC) all


# Demo widgets target
demo: $(LVGL_OBJS) $(LVGL_DEMO_OBJS)
	@echo "Compiling demo..."
	$(CXX) -o $(BIN_DIR)/lvgl-demo demo-widgets.cpp $(LVGL_OBJS) $(LVGL_DEMO_OBJS) \
	  $(LVGL_INC) $(SDL2_CFLAGS) $(CXXFLAGS) $(SDL2_LIBS)
	@echo "✓ Demo ready: ./build/bin/lvgl-demo"
	@echo ""
	@echo "Run: ./build/bin/lvgl-demo"

$(OBJ_DIR)/lvgl/demos/%.o: $(LVGL_DIR)/demos/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL demo: $<..."
	$(CC) -c $< -o $@ $(LVGL_INC) $(CFLAGS)

# Test targets
test: $(TEST_BIN)
	@echo "Running unit tests..."
	@$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS) $(LVGL_OBJS) $(OBJ_DIR)/ui_nav.o
	@mkdir -p $(BIN_DIR)
	@echo "Linking test binary..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Test binary ready: $@"

$(OBJ_DIR)/tests/%.o: $(TEST_UNIT_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling test: $<..."
	@$(CXX) $(CXXFLAGS) -I$(TEST_FRAMEWORK_DIR) $(INCLUDES) $(LV_CONF) -c $< -o $@

