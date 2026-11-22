#include "core/touch.h"

#include <algorithm>

namespace amr {

Touch::Touch() {
    // game/touch.py's layout in units of a tenth of the screen height (18 px here), margin 0.4 of one
    constexpr int m = 7;
    buttons_ = {
        {"left", "<", m, kH - m - 50, 50, 50, true},
        {"right", ">", m + 56, kH - m - 50, 50, 50, true},
        {"gas", "GAS", kW - m - 54, kH - m - 79, 54, 79, true},
        {"brake", "BRAKE", kW - m - 54 - 5 - 43, kH - m - 43, 43, 43, true},
        {"hand", "HAND", kW - m - 54 - 5 - 43, kH - m - 43 - 5 - 29, 43, 29, true},
        {"hop", "HOP", kW - m - 54 - 5 - 43, kH - m - 43 - 5 - 29 - 5 - 22, 43, 22, true},
        {"tag", "TAG", kW - m - 54, kH - m - 79 - 5 - 22, 54, 22, true},
        {"pause", "PAUSE", m, 22, 34, 14, false},
    };
}

void Touch::to_picture(float wx, float wy, int window_w, int window_h, float& px, float& py, float margin) {
    const int aw = static_cast<int>(window_w * (1 - 2 * margin)), ah = static_cast<int>(window_h * (1 - 2 * margin));
    int dw = aw, dh = aw * kH / kW;
    if (dh > ah) {
        dh = ah;
        dw = ah * kW / kH;
    }
    const float x0 = (window_w - dw) / 2.0f, y0 = (window_h - dh) / 2.0f;
    px = std::clamp((wx - x0) * kW / std::max(1, dw), 0.0f, kW - 0.01f);
    py = std::clamp((wy - y0) * kH / std::max(1, dh), 0.0f, kH - 0.01f);
}

void Touch::handle(const SDL_Event& e, int window_w, int window_h) {
    float px, py;
    switch (e.type) {
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
            finger_seen_ = shown_ = true;
            fingers_.erase(-1);
            to_picture(e.tfinger.x * window_w, e.tfinger.y * window_h, window_w, window_h, px, py, margin_);
            fingers_[static_cast<long long>(e.tfinger.fingerId)] = {px, py};
            if (e.type == SDL_FINGERDOWN) taps_.push_back({static_cast<int>(px), static_cast<int>(py)});
            break;
        case SDL_FINGERUP:
            fingers_.erase(static_cast<long long>(e.tfinger.fingerId));
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONUP: {
            // a phone's touches also arrive as made-up mouse events: never count those twice
            const bool from_touch = (e.type == SDL_MOUSEMOTION ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID;
            if (!mouse_ || from_touch || finger_seen_) break;
            if (e.type == SDL_MOUSEBUTTONUP) { fingers_.erase(-1); break; }
            if (e.type == SDL_MOUSEBUTTONDOWN) shown_ = true;
            const int x = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
            const int y = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
            if (e.type == SDL_MOUSEMOTION && !(e.motion.state & SDL_BUTTON_LMASK)) break;
            to_picture(static_cast<float>(x), static_cast<float>(y), window_w, window_h, px, py, margin_);
            fingers_[-1] = {px, py};
            if (e.type == SDL_MOUSEBUTTONDOWN) taps_.push_back({static_cast<int>(px), static_cast<int>(py)});
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) fingers_.clear();  // never leave a button stuck
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            fingers_.clear();
            break;
        default: break;
    }
}

std::vector<std::string> Touch::update() {
    std::vector<std::string> now;
    for (const auto& f : fingers_)
        for (const TouchButton& b : buttons_) {
            // forgiving for thumbs: 3 px more all round, and buttons near the picture's edge reach right to it (and
            // beyond, onto a phone's black bars, which map to the edge)
            const int x0 = b.x <= 8 ? 0 : b.x - 3, y0 = b.y <= 8 ? 0 : b.y - 3;
            const int x1 = b.x + b.w >= kW - 8 ? kW : b.x + b.w + 3, y1 = b.y + b.h >= kH - 8 ? kH : b.y + b.h + 3;
            if (f.second.first >= x0 && f.second.first < x1 && f.second.second >= y0 && f.second.second < y1 &&
                std::find(now.begin(), now.end(), b.name) == now.end())
                now.push_back(b.name);
        }
    std::vector<std::string> tapped;
    for (const TouchButton& b : buttons_)
        if (!b.held && std::find(now.begin(), now.end(), b.name) != now.end() &&
            std::find(prev_.begin(), prev_.end(), b.name) == prev_.end())
            tapped.push_back(b.name);
    if (!shown_) now.clear();  // hidden: fingers do nothing (a pad is in use)
    pressed_ = now;
    prev_ = now;
    return shown_ ? tapped : std::vector<std::string>{};
}

bool Touch::pressed(const std::string& name) const {
    return std::find(pressed_.begin(), pressed_.end(), name) != pressed_.end();
}

std::vector<std::pair<int, int>> Touch::taps() {
    std::vector<std::pair<int, int>> t;
    t.swap(taps_);
    return t;
}

}  // namespace amr
