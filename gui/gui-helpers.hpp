#pragma once
#include <vector>
#include <imgui/imgui.h>
#include "utility.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui/imgui_internal.h"

static inline ImVec2 operator+(const ImVec2& lhs, const float scalar) { return ImVec2(lhs.x + scalar, lhs.y + scalar); }
static inline ImVec2 operator-(const ImVec2& lhs, const float scalar) { return ImVec2(lhs.x - scalar, lhs.y - scalar); }
static inline ImVec2& operator+=(ImVec2& lhs, const float scalar) { lhs.x += scalar; lhs.y += scalar; return lhs; }
static inline ImVec2& operator-=(ImVec2& lhs, const float scalar) { lhs.x -= scalar; lhs.y -= scalar; return lhs; }

bool CustomListBoxInt(const char* label, int* value, const std::vector<const char*> list, float width = 225.f, ImGuiComboFlags flags = ImGuiComboFlags_None);
bool CustomListBoxIntMultiple(const char* label, std::vector<std::pair<const char*, bool>>* list, float width, bool resetButton = true, ImGuiComboFlags flags = ImGuiComboFlags_None);
bool SteppedSliderFloat(const char* label, float* v, float v_min, float v_max, float v_step, const char* format, ImGuiSliderFlags flags);
bool HotKey(uint8_t& key);