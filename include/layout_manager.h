// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

class LayoutManagerTestAccess;

namespace helix {

enum class LayoutType {
    STANDARD,     // 4:3 to ~16:9
    ULTRAWIDE,    // >2.5:1 aspect ratio
    PORTRAIT,     // <0.8:1
    MICRO,        // landscape, 480x272 class
    MICRO_PORTRAIT, // portrait, 480x272 class
    TINY,         // landscape, max dim <=480
    TINY_PORTRAIT // portrait, max dim <=480
};

class LayoutManager {
  public:
    static LayoutManager& instance();

    void init(int width, int height);
    void set_override(const std::string& name);

    LayoutType type() const;
    const std::string& name() const;
    std::string resolve_xml_path(const std::string& filename) const;
    bool has_override(const std::string& filename) const;
    bool is_standard() const;

  private:
    friend class ::LayoutManagerTestAccess;
    LayoutManager() = default;
    LayoutType detect(int width, int height) const;
    static const char* type_to_name(LayoutType type);
    static LayoutType name_to_type(const std::string& name);

    LayoutType type_{LayoutType::STANDARD};
    std::string name_{"standard"};
    std::string override_name_;
    bool initialized_{false};
};

} // namespace helix
