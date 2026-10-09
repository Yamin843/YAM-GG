#include "Notification.h"

#include "imgui.h"

namespace yamgg {

Notification& Notification::instance() {
    static Notification inst;
    return inst;
}

void Notification::push(const std::string& msg, float duration) {
    Item it;
    it.id = next_id_++;
    it.msg = msg;
    it.created = std::chrono::steady_clock::now();
    it.duration = duration > 0.0f ? duration : 3.0f;
    items_.push_back(std::move(it));
    if (items_.size() > 8) items_.pop_front();
}

void Notification::draw() {
    auto now = std::chrono::steady_clock::now();
    while (!items_.empty()) {
        float age = std::chrono::duration<float>(now - items_.front().created).count();
        if (age > items_.front().duration) items_.pop_front();
        else break;
    }

    if (items_.empty()) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + 20.0f;
    const float x = vp->WorkPos.x + vp->WorkSize.x - 20.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.03f, 0.03f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.65f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        float age = std::chrono::duration<float>(now - it->created).count();
        float alpha = 1.0f;
        float fadeStart = it->duration - 0.6f;
        if (age > fadeStart && fadeStart > 0.0f) {
            alpha = 1.0f - (age - fadeStart) / 0.6f;
            if (alpha < 0.0f) alpha = 0.0f;
        }
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        char name[64];
        snprintf(name, sizeof(name), "##notif_%d", it->id);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin(name, nullptr, flags)) {
            ImGui::TextWrapped("%s", it->msg.c_str());
        }
        ImGui::End();
        y += ImGui::GetTextLineHeightWithSpacing() + 26.0f;
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void Notification::clear() {
    items_.clear();
}

} // namespace yamgg
