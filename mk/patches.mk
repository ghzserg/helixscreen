# Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen UI Prototype - Upstream Patch Management Module
# Handles automatic application of patches to LVGL and other dependencies

# Files modified by LVGL patches (used by reset-patches)
LVGL_PATCHED_FILES := \
	src/drivers/sdl/lv_sdl_window.c \
	src/themes/default/lv_theme_default.c \
	src/xml/parsers/lv_xml_image_parser.c \
	src/xml/lv_xml_style.c \
	src/xml/lv_xml.c \
	src/xml/lv_xml.h \
	src/drivers/display/fb/lv_linux_fbdev.c \
	src/core/lv_refr.c \
	src/core/lv_observer.c

# Files modified by libhv patches
LIBHV_PATCHED_FILES := \
	http/client/requests.h

# Reset all patched files in LVGL submodule to upstream state
reset-patches:
	$(ECHO) "$(YELLOW)Resetting LVGL patches to upstream state...$(RESET)"
	$(Q)for file in $(LVGL_PATCHED_FILES); do \
		if ! git -C $(LVGL_DIR) diff --quiet $$file 2>/dev/null; then \
			echo "$(YELLOW)→ Resetting:$(RESET) $$file"; \
			git -C $(LVGL_DIR) checkout $$file; \
		else \
			echo "$(DIM)  (clean) $$file$(RESET)"; \
		fi \
	done
	$(ECHO) "$(GREEN)✓ All LVGL patches reset$(RESET)"

# Force reapply all patches (reset first, then apply)
reapply-patches: reset-patches apply-patches
	$(ECHO) "$(GREEN)✓ All patches reapplied$(RESET)"

# Apply LVGL patches if not already applied
apply-patches:
	$(ECHO) "$(CYAN)Checking LVGL patches...$(RESET)"
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/drivers/sdl/lv_sdl_window.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL SDL window position patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_sdl_window_position.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_sdl_window_position.patch && \
			echo "$(GREEN)✓ SDL window position patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL SDL window position patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/themes/default/lv_theme_default.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL theme breakpoints patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_theme_breakpoints.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_theme_breakpoints.patch && \
			echo "$(GREEN)✓ Theme breakpoints patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL theme breakpoints patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/xml/parsers/lv_xml_image_parser.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL image parser contain/cover patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_image_parser_contain.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_image_parser_contain.patch && \
			echo "$(GREEN)✓ Image parser contain/cover patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL image parser contain/cover patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/xml/lv_xml_style.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL translate percentage patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_translate_percent.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_translate_percent.patch && \
			echo "$(GREEN)✓ Translate percentage patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL translate percentage patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/drivers/display/fb/lv_linux_fbdev.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL fbdev stride bpp detection patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_fbdev_stride_bpp.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_fbdev_stride_bpp.patch && \
			echo "$(GREEN)✓ Fbdev stride bpp detection patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL fbdev stride bpp detection patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/xml/lv_xml.h 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL XML silent const lookup patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_xml_const_silent.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_xml_const_silent.patch && \
			echo "$(GREEN)✓ XML silent const lookup patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL XML silent const lookup patch already applied$(RESET)"; \
	fi
	$(Q)if git -C $(LVGL_DIR) diff --quiet src/core/lv_observer.c 2>/dev/null; then \
		echo "$(YELLOW)→ Applying LVGL observer debug info patch...$(RESET)"; \
		if git -C $(LVGL_DIR) apply --check ../../patches/lvgl_observer_debug.patch 2>/dev/null; then \
			git -C $(LVGL_DIR) apply ../../patches/lvgl_observer_debug.patch && \
			echo "$(GREEN)✓ Observer debug info patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ LVGL observer debug info patch already applied$(RESET)"; \
	fi
	$(ECHO) "$(CYAN)Checking libhv patches...$(RESET)"
	$(Q)if git -C $(LIBHV_DIR) diff --quiet http/client/requests.h 2>/dev/null; then \
		echo "$(YELLOW)→ Applying libhv streaming upload patch...$(RESET)"; \
		if git -C $(LIBHV_DIR) apply --check ../../patches/libhv-streaming-upload.patch 2>/dev/null; then \
			git -C $(LIBHV_DIR) apply ../../patches/libhv-streaming-upload.patch && \
			echo "$(GREEN)✓ libhv streaming upload patch applied$(RESET)"; \
		else \
			echo "$(YELLOW)⚠ Cannot apply patch (already applied or conflicts)$(RESET)"; \
		fi \
	else \
		echo "$(GREEN)✓ libhv streaming upload patch already applied$(RESET)"; \
	fi
	$(Q)if [ -d "$(LIBHV_DIR)/include/hv" ]; then \
		if ! diff -q "$(LIBHV_DIR)/http/client/requests.h" "$(LIBHV_DIR)/include/hv/requests.h" >/dev/null 2>&1; then \
			echo "$(YELLOW)→ Syncing patched requests.h to include/hv/$(RESET)"; \
			cp "$(LIBHV_DIR)/http/client/requests.h" "$(LIBHV_DIR)/include/hv/requests.h" && \
			echo "$(GREEN)✓ Patched header synced$(RESET)"; \
		fi \
	fi
