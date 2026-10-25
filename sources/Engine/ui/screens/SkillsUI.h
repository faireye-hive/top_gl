#pragma once
#include "../UITheme.h"
#include <string>
#include <utility>
#include <vector>

namespace top::ui {
struct HudDrawList;
struct WindowState;
struct UIContentState;
class UIRenderer;
struct WindowState;

struct SkillView {
    int id = 0;
    std::string name;
    std::string icon;
    int sp = 0;
    bool learned = false;
    bool cooldown = false;
    int hotkey = -1;
    int level = 1;
};

class SkillsUI {
public:
    void appendSprites(HudDrawList& out, const WindowState& windows, int screenW) const;
    void appendText(HudDrawList& out, const WindowState& windows, int screenW, float lineHeight) const;
    static void draw(UIRenderer& r, float x, float y, int page, int count, const UITheme& t = {});
    void setView(std::vector<SkillView> skills, int points);
    void setLines(std::vector<std::string> lines) { lines_ = std::move(lines); }
    void clearLines() { lines_.clear(); }
    int hit(int x, int y, int screenW, const WindowState& windows) const;
    int upgradeHit(int x, int y, int screenW, const WindowState& windows) const;
    bool wheel(int x, int y, int screenW, int delta, const WindowState& windows);
    int page() const { return page_; }
    int points() const { return points_; }
    const std::vector<SkillView>& rows() const { return rows_; }
    const std::vector<std::string>& lines() const { return lines_; }
    void appendLine(std::string line) { lines_.push_back(std::move(line)); }
private:
    std::vector<SkillView> rows_;
    std::vector<std::string> lines_;
    int points_ = 0;
    int page_ = 0;
};
} // namespace top::ui
