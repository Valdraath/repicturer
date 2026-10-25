// tooltips that always fit: wrapped to the window they are shown in (Preferences, the guides and the explorer
// are small os windows of their own, a wide tooltip was cut off at their edge) and to a readable line length

#pragma once

#include <algorithm>
#include <stdarg.h>
#include <stdio.h>

#include "imgui.h"

inline void TooltipText(const char* fmt, ...)
{
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fit = ImGui::GetMainViewport()->Size.x - 2 * st.WindowPadding.x - 2 * st.DisplaySafeAreaPadding.x - 8.0f;
    // at most about 60 % of the window (and a readable line length), so it fits next to the cursor and moves with it
    const float wrap = std::max(ImGui::GetFontSize() * 12, std::min(fit * 0.6f, ImGui::GetFontSize() * 30));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(buf);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
