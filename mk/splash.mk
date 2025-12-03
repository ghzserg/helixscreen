# SPDX-License-Identifier: GPL-3.0-or-later
# Splash screen binary build rules
#
# Builds helix-splash, a minimal splash screen for embedded targets.
# This binary starts instantly while the main app initializes in parallel.
#
# Only built for embedded Linux targets (not macOS/desktop).

SPLASH_SRC := src/helix_splash.cpp
SPLASH_OBJ := $(BUILD_DIR)/splash/helix_splash.o
SPLASH_BIN := $(BUILD_DIR)/bin/helix-splash

# Splash needs LVGL, display library, project includes, and libhv (for config.h -> json.hpp)
SPLASH_CXXFLAGS := $(CXXFLAGS) -I$(INC_DIR) $(LVGL_INC) $(SPDLOG_INC) $(LIBHV_INC) -DHELIX_SPLASH_ONLY
# Note: LVGL is compiled as objects, not a library - link directly against LVGL_OBJS
SPLASH_LDFLAGS := -lm -lpthread

# Strip binary for embedded targets (matches main binary behavior)
ifeq ($(STRIP_BINARY),yes)
    SPLASH_LDFLAGS += -s
endif

# Add platform-specific libraries for display backend
ifneq ($(UNAME_S),Darwin)
    # Linux: may need DRM/input libraries
    ifeq ($(DISPLAY_BACKEND),drm)
        SPLASH_LDFLAGS += -ldrm -linput
    endif
endif

# Only build splash for embedded targets (not macOS)
# On macOS, the main app handles its own splash via SDL
ifneq ($(UNAME_S),Darwin)

# Compile splash source
$(BUILD_DIR)/splash/%.o: src/%.cpp | $(BUILD_DIR)/splash
	@echo "[CXX] $< (splash)"
	$(Q)$(CXX) $(SPLASH_CXXFLAGS) -c $< -o $@

# Splash needs config.o (display_backend_drm.cpp uses Config) and a UI notification stub
# (config.cpp calls ui_notification_error on save failures)
SPLASH_EXTRA_OBJS := $(OBJ_DIR)/config.o $(BUILD_DIR)/splash/ui_notification_stub.o

# Compile notification stub for splash
$(BUILD_DIR)/splash/ui_notification_stub.o: tools/ui_notification_stub.cpp | $(BUILD_DIR)/splash
	@echo "[CXX] $< (splash stub)"
	$(Q)$(CXX) $(SPLASH_CXXFLAGS) -c $< -o $@

# Link splash binary
# Dependencies: splash object, display library, LVGL objects (compiled from source)
$(SPLASH_BIN): $(SPLASH_OBJ) $(DISPLAY_LIB) $(LVGL_OBJS) $(THORVG_OBJS) $(SPLASH_EXTRA_OBJS) | $(BUILD_DIR)/bin
	@echo "[LD] $@"
	$(Q)$(CXX) $(SPLASH_OBJ) $(DISPLAY_LIB) $(LVGL_OBJS) $(THORVG_OBJS) $(SPLASH_EXTRA_OBJS) -o $@ $(SPLASH_LDFLAGS)

# Create build directories
$(BUILD_DIR)/splash $(BUILD_DIR)/bin:
	$(Q)mkdir -p $@

# Build splash binary
.PHONY: splash
splash: $(SPLASH_BIN)
	@echo "Built: $(SPLASH_BIN)"

# Clean splash build artifacts
.PHONY: clean-splash
clean-splash:
	$(Q)rm -rf $(BUILD_DIR)/splash $(SPLASH_BIN)

else
# macOS: no-op targets

.PHONY: splash clean-splash
splash:
	@echo "Splash binary not built on macOS (uses SDL splash in main app)"

clean-splash:
	@true

endif  # not Darwin

# Help text
.PHONY: help-splash
help-splash:
	@echo "Splash screen targets:"
	@echo "  splash        - Build helix-splash binary (embedded only)"
	@echo "  clean-splash  - Remove splash build artifacts"
