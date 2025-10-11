# LVGL 9 XML UI Prototype - Development Roadmap

## ✅ Phase 1: Foundation (COMPLETED)

- [x] LVGL 9.3 setup with XML support
- [x] Navigation bar with proper flex layout
- [x] Home panel with declarative XML
- [x] Reactive data binding (Subject-Observer pattern)
- [x] Theme system with global constants
- [x] FontAwesome icon integration with auto-generation
- [x] Screenshot utility supporting multiple binaries
- [x] Comprehensive documentation
- [x] C++ wrappers for XML panels with clean API
- [x] LVGL logging integration

## ✅ Phase 2: Navigation & Blank Panels (COMPLETED)

**Priority: High** - Enable panel switching with reactive navbar highlighting

- [x] **Navigation State Management**
  - Active panel tracking via integer subject
  - Navbar button click handlers (C++ event handlers)
  - Panel visibility toggling (show/hide based on active state)
  - Reactive navbar icon highlighting (red #ff4444 for active, white #ffffff for inactive)

- [x] **Blank Placeholder Panels**
  - Home panel (with printer status content)
  - Controls panel (blank placeholder)
  - Filament panel (blank placeholder)
  - Settings panel (blank placeholder)
  - Advanced panel (blank placeholder)

- [x] **Navigation Implementation**
  - Updated navigation_bar.xml with FontAwesome icons
  - Created C++ wrapper (ui_nav.cpp) with Subject-Observer pattern
  - Panel switching via clickable navigation icons
  - Icon color updates based on active panel

**Key Learnings:**
- Never call `SDL_PollEvent()` manually - violates LVGL display driver abstraction
- Must create mouse input device with `lv_sdl_mouse_create()` for clicks
- Event handlers in C++ with `lv_obj_add_event_cb()` for `LV_EVENT_CLICKED`
- Labels must have `LV_OBJ_FLAG_EVENT_BUBBLE` and not be clickable for clicks to reach buttons

## 📋 Phase 3: Additional Panel Content (IN PROGRESS)

**Priority: High** - Build out remaining screens

- [ ] **Print Status Panel** - Active print monitoring
  - Progress bar (integer subject with `bind_value`)
  - Print time/remaining time
  - Bed/nozzle temperatures with reactive updates
  - Pause/Resume/Cancel buttons

- [ ] **Controls Panel** - Manual printer control
  - Movement controls (X/Y/Z axes)
  - Extrusion controls
  - Temperature presets
  - Home buttons

- [ ] **Filament Panel** - Filament management
  - Load/unload controls
  - Filament profiles
  - Color/material selection

- [ ] **Settings Panel** - Configuration
  - Network settings
  - Display settings (brightness, theme)
  - Printer settings
  - System info

- [ ] **Advanced/Tools Panel** - Advanced features
  - Bed mesh visualization
  - Console/logs
  - File manager
  - System controls

## 🎨 Phase 4: Enhanced UI Components

**Priority: Medium** - Richer interactions

- [ ] **Integer Subjects for Numeric Displays**
  - Progress bars with `bind_value`
  - Sliders with bi-directional binding
  - Arc/gauge widgets

- [ ] **Custom Widgets as XML Components**
  - Temperature display widget
  - Print progress widget
  - Status badge component

- [ ] **Modal Dialogs/Popups**
  - Confirmation dialogs
  - Error messages
  - Loading indicators

- [ ] **Lists and Scrolling**
  - File list for print files
  - Settings menu items
  - Log viewer

## 🔗 Phase 5: Panel Transitions & Polish

**Priority: Medium** - Visual refinement

- [ ] **Panel Transitions**
  - Fade in/out animations
  - Slide animations
  - Proper cleanup when switching panels

- [ ] **Button Feedback**
  - Navbar button press effects
  - Hover states (if applicable)
  - Ripple effects

## 🔌 Phase 6: Backend Integration

**Priority: Medium** - Connect to Klipper/Moonraker

- [ ] **WebSocket Client for Moonraker**
  - Connection management
  - JSON-RPC protocol
  - Reconnection logic

- [ ] **Printer State Management**
  - Poll printer status
  - Update subjects from printer data
  - Handle state changes

- [ ] **File Operations**
  - List print files
  - Upload files
  - Delete files
  - Start prints

- [ ] **Real-time Updates**
  - Temperature monitoring
  - Print progress
  - Status changes

## 🎭 Phase 7: Theming & Accessibility

**Priority: Low** - Visual refinement

- [ ] **Theme Variants**
  - Light mode
  - High contrast mode
  - Custom color schemes

- [ ] **Responsive Layouts**
  - Support multiple resolutions
  - Orientation changes
  - Scaling for different DPI

- [ ] **Animations**
  - Button press effects
  - Panel transitions
  - Loading animations
  - Success/error feedback

- [ ] **Accessibility**
  - Larger touch targets
  - High contrast options
  - Status announcements

## 🧪 Phase 8: Testing & Optimization

**Priority: Ongoing** - Quality assurance

- [ ] **Memory Profiling**
  - Check for leaks
  - Optimize subject usage
  - Profile panel switching

- [ ] **Performance Testing**
  - Frame rate monitoring
  - UI responsiveness
  - Touch latency

- [ ] **Cross-platform Testing**
  - Raspberry Pi target
  - BTT Pad target
  - Different screen sizes

- [ ] **Edge Case Handling**
  - Connection loss
  - Print errors
  - File system errors

## 🚀 Phase 9: Production Readiness

**Priority: Future** - Deployment prep

- [ ] **Configuration System**
  - Runtime configuration file
  - Printer profiles
  - User preferences

- [ ] **Error Handling**
  - Graceful degradation
  - Error recovery
  - User notifications

- [ ] **Logging System**
  - Structured logging
  - Log levels
  - Log rotation

- [ ] **Build System Improvements**
  - Cross-compilation setup
  - Packaging for targets
  - Install scripts

---

## Current Status

**Active Phase:** Phase 3 - Additional Panel Content

**Next Milestone:** Build out content for remaining panels (Controls, Filament, Settings, Advanced)

**Completed Phases:** 1, 2

**Phase 2 Completion Date:** 2025-10-08

---

## Notes

- **Reactive Pattern:** All UI updates should use Subject-Observer pattern
- **XML First:** Prefer XML layout over C++ when possible
- **Clean Separation:** Keep business logic in C++, layout in XML
- **Documentation:** Update guides as patterns emerge
- **Testing:** Test each phase before moving to next

---

**Last Updated:** 2025-10-08
