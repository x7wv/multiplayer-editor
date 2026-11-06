// Multiplayer Editor — made by x7wv
// Tool screens: hub, timeline, tasks, votes, templates, group IDs, leaderboard, timelapse.
#include "Common.hpp"
#include "Session.hpp"
#include "Json.hpp"

using namespace geode::prelude;

namespace me {

static std::string ago(double tMs) {
    double s = (wallMs() - tMs) / 1000.0;
    if (s < 60) return fmt::format("{}s ago", static_cast<int>(std::max(0.0, s)));
    if (s < 3600) return fmt::format("{}m ago", static_cast<int>(s / 60));
    if (s < 86400) return fmt::format("{}h ago", static_cast<int>(s / 3600));
    return fmt::format("{}d ago", static_cast<int>(s / 86400));
}

// A popup with a paged list area and data refresh from the session.
class ListPopup : public BasePopup {
protected:
    CCNode* m_list = nullptr;
    CCMenu* m_listMenu = nullptr;
    int m_page = 0;
    int m_perPage = 6;
    float m_rowH = 30.f;
    float m_top = 60.f;

    bool setupList(float w, float h, char const* title, int perPage, float top = 60.f) {
        if (!setup(w, h, title)) return false;
        m_perPage = perPage;
        m_top = top;
        m_list = CCNode::create();
        m_mainLayer->addChild(m_list, 5);
        m_listMenu = CCMenu::create();
        m_listMenu->setPosition({0, 0});
        m_mainLayer->addChild(m_listMenu, 6);
        button("<", {20, m_size.height / 2}, [this](auto) { if (m_page > 0) { m_page--; rebuild(); } }, .5f, "GJ_button_02.png");
        button(">", {m_size.width - 20, m_size.height / 2}, [this](auto) { m_page++; rebuild(); }, .5f, "GJ_button_02.png");
        Session::get().onData = [this] { rebuild(); };
        return true;
    }
    ~ListPopup() override { Session::get().onData = nullptr; }

    virtual int count() = 0;
    virtual void row(int i, float y) = 0;

    void rebuild() {
        m_list->removeAllChildren();
        m_listMenu->removeAllChildren();
        int n = count();
        int pages = std::max(1, (n + m_perPage - 1) / m_perPage);
        m_page = std::clamp(m_page, 0, pages - 1);
        float y = m_size.height - m_top;
        for (int i = m_page * m_perPage; i < std::min(n, (m_page + 1) * m_perPage); i++) {
            row(i, y);
            y -= m_rowH;
        }
        if (n == 0) addText("Nothing here yet", {m_size.width / 2, m_size.height / 2}, .4f, "bigFont.fnt", {180, 180, 180});
        addText(fmt::format("page {}/{}", m_page + 1, pages), {m_size.width - 50, 14}, .4f, "chatFont.fnt", {170, 170, 170});
    }
    CCLabelBMFont* addText(std::string const& t, CCPoint pos, float scale, char const* font = "chatFont.fnt", ccColor3B col = {255, 255, 255}, CCPoint anchor = {.5f, .5f}, float maxW = 0) {
        auto l = label(t, font, scale, col);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        if (maxW > 0) l->limitLabelWidth(maxW, scale, .1f);
        m_list->addChild(l);
        return l;
    }
    float addBtnRight(float& x, float y, char const* t, geode::Function<void(CCMenuItemSpriteExtra*)> cb, char const* bg = "GJ_button_04.png") {
        auto b = textButton(t, std::move(cb), .38f, bg);
        x -= b->getScaledContentSize().width / 2;
        b->setPosition({x, y});
        x -= b->getScaledContentSize().width / 2 + 3;
        m_listMenu->addChild(b);
        return x;
    }
};

// ---------------------------------------------------------------- timeline
class TimelinePopup : public ListPopup {
public:
    static TimelinePopup* create() { auto r = new TimelinePopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    bool initPopup() {
        if (!setupList(440, 290, "Timeline", 7, 55)) return false;
        m_rowH = 27;
        button("Load Older", {m_size.width / 2 - 70, 22}, [](auto) {
            auto& h = Session::get().m_history;
            if (!h.empty()) Session::get().requestHistory(h.back().q);
        }, .45f);
        button("Refresh", {m_size.width / 2 + 50, 22}, [](auto) { Session::get().requestHistory(); }, .45f);
        Session::get().requestHistory();
        rebuild();
        return true;
    }
    int count() override { return static_cast<int>(Session::get().m_history.size()); }
    void row(int i, float y) override {
        auto& s = Session::get();
        auto const& h = s.m_history[i];
        std::string what;
        if (h.add) what += fmt::format("+{} ", h.add);
        if (h.del) what += fmt::format("-{} ", h.del);
        if (h.upd) what += fmt::format("~{}", h.upd);
        addText(fmt::format("#{}  {}", h.q, h.name), {40, y + 5}, .5f, "chatFont.fnt", theme().accent2, {0, .5f}, 200);
        addText(fmt::format("{}  {}  {}", what, ago(h.t), h.tag), {40, y - 7}, .4f, "chatFont.fnt", {190, 190, 190}, {0, .5f}, 240);
        if (s.m_myRole == "host") {
            int q = h.q;
            float x = m_size.width - 35;
            addBtnRight(x, y, "Restore here", [q](auto) {
                createQuickPopup("Time Travel", fmt::format("Restore the level to how it was right after <cy>#{}</c>?\nEveryone's level updates, and you can undo it from the timeline.", q),
                    "Cancel", "Restore", [q](auto, bool yes) { if (yes) Session::get().restore(q); });
            }, "GJ_button_02.png");
        }
    }
};

// ---------------------------------------------------------------- tasks
class TasksPopup : public ListPopup {
public:
    static TasksPopup* create() { auto r = new TasksPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    TextInput* m_in = nullptr;
    bool initPopup() {
        if (!setupList(440, 290, "Build Requests", 6, 85)) return false;
        m_in = input("What's needed here? (e.g. needs deco)", {m_size.width / 2 - 50, m_size.height - 52}, 280);
        button("Add (this area)", {m_size.width - 75, m_size.height - 52}, [this](auto) {
            std::string t = m_in->getString();
            if (t.empty()) t = "Needs work";
            Session::get().taskOp("add", "", t);
            m_in->setString("");
        }, .45f);
        rebuild();
        return true;
    }
    int count() override { return static_cast<int>(Session::get().m_tasks.size()); }
    void row(int i, float y) override {
        auto const& t = Session::get().m_tasks[i];
        ccColor3B col = t.done ? ccColor3B{120, 255, 120} : t.claim.empty() ? ccColor3B{255, 220, 80} : ccColor3B{120, 200, 255};
        addText((t.done ? "[done] " : "") + t.text, {40, y + 5}, .5f, "chatFont.fnt", col, {0, .5f}, 190);
        addText(fmt::format("by {}{}", t.by, t.claim.empty() ? "" : "  |  claimed by " + t.claim), {40, y - 7}, .4f, "chatFont.fnt", {190, 190, 190}, {0, .5f}, 190);
        float x = m_size.width - 35;
        auto id = t.id;
        CCPoint mid((t.rect[0] + t.rect[2]) / 2, (t.rect[1] + t.rect[3]) / 2);
        addBtnRight(x, y, "Del", [id](auto) { Session::get().taskOp("del", id); }, "GJ_button_06.png");
        addBtnRight(x, y, t.done ? "Undone" : "Done", [id](auto) { Session::get().taskOp("done", id); });
        addBtnRight(x, y, "Claim", [id](auto) { Session::get().taskOp("claim", id); });
        addBtnRight(x, y, "Go", [mid, this](auto) { Session::get().goTo(mid); this->onClose(nullptr); }, "GJ_button_01.png");
    }
};

// ---------------------------------------------------------------- votes
class VotesPopup : public ListPopup {
public:
    static VotesPopup* create() { auto r = new VotesPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    TextInput* m_in = nullptr;
    bool initPopup() {
        if (!setupList(440, 290, "Votes", 6, 85)) return false;
        m_in = input("Ask the room... (e.g. keep this part?)", {m_size.width / 2 - 45, m_size.height - 52}, 290);
        button("Start Vote", {m_size.width - 65, m_size.height - 52}, [this](auto) {
            std::string q = m_in->getString();
            if (q.empty()) q = "Keep this part?";
            Session::get().voteOp("new", "", 0, q);
            m_in->setString("");
        }, .45f);
        rebuild();
        return true;
    }
    int count() override { return static_cast<int>(Session::get().m_votes.size()); }
    void row(int i, float y) override {
        auto& s = Session::get();
        int idx = count() - 1 - i; // newest first
        auto const& v = s.m_votes[idx];
        addText(v.q, {40, y + 5}, .5f, "chatFont.fnt", v.open ? ccColor3B{255, 255, 255} : ccColor3B{160, 160, 160}, {0, .5f}, 190);
        addText(fmt::format("Yes {}  |  No {}  |  by {}{}", v.yes, v.no, v.by, v.open ? "" : "  |  closed"), {40, y - 7}, .4f, "chatFont.fnt",
            v.yes >= v.no ? ccColor3B{120, 255, 120} : ccColor3B{255, 140, 120}, {0, .5f}, 200);
        float x = m_size.width - 35;
        auto id = v.id;
        if (s.m_myRole == "host") addBtnRight(x, y, "Del", [id](auto) { Session::get().voteOp("del", id); }, "GJ_button_06.png");
        if (v.open) {
            addBtnRight(x, y, "Close", [id](auto) { Session::get().voteOp("close", id); });
            addBtnRight(x, y, "No", [id](auto) { Session::get().voteOp("cast", id, -1); }, "GJ_button_06.png");
            addBtnRight(x, y, "Yes", [id](auto) { Session::get().voteOp("cast", id, 1); }, "GJ_button_01.png");
        }
    }
};

// ---------------------------------------------------------------- templates
class TemplatesPopup : public ListPopup {
public:
    static TemplatesPopup* create() { auto r = new TemplatesPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    TextInput* m_in = nullptr;
    bool initPopup() {
        if (!setupList(420, 290, "Template Library", 6, 85)) return false;
        m_in = input("Template name", {m_size.width / 2 - 55, m_size.height - 52}, 240);
        button("Save Selected", {m_size.width - 75, m_size.height - 52}, [this](auto) {
            Session::get().saveTemplate(m_in->getString());
            m_in->setString("");
        }, .45f);
        Session::get().requestTemplates();
        rebuild();
        return true;
    }
    int count() override { return static_cast<int>(Session::get().m_templates.size()); }
    void row(int i, float y) override {
        auto const& t = Session::get().m_templates[count() - 1 - i];
        addText(t.name, {40, y + 5}, .5f, "chatFont.fnt", theme().accent2, {0, .5f}, 200);
        addText(fmt::format("{} objects  |  by {}", t.count, t.by), {40, y - 7}, .4f, "chatFont.fnt", {190, 190, 190}, {0, .5f}, 200);
        float x = m_size.width - 35;
        auto id = t.id;
        addBtnRight(x, y, "Del", [id](auto) { Session::get().deleteTemplate(id); }, "GJ_button_06.png");
        addBtnRight(x, y, "Paste", [id, this](auto) { Session::get().pasteTemplate(id); this->onClose(nullptr); }, "GJ_button_01.png");
    }
};

// ---------------------------------------------------------------- group ids
class GroupsPopup : public ListPopup {
public:
    static GroupsPopup* create() { auto r = new GroupsPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    std::vector<std::pair<int, std::string>> m_rows;
    bool initPopup() {
        if (!setupList(400, 280, "Group ID Manager", 8, 95)) return false;
        m_rowH = 18;
        text("Get group IDs nobody else is using:", {m_size.width / 2, m_size.height - 45}, .45f, "chatFont.fnt");
        button("Get 1", {m_size.width / 2 - 80, m_size.height - 68}, [](auto) { Session::get().requestGroups(1); }, .45f);
        button("Get 5", {m_size.width / 2, m_size.height - 68}, [](auto) { Session::get().requestGroups(5); }, .45f);
        button("Get 10", {m_size.width / 2 + 80, m_size.height - 68}, [](auto) { Session::get().requestGroups(10); }, .45f);
        Session::get().sendRaw(matjson::makeObject({{"t", "groupClaims"}}));
        rebuild();
        return true;
    }
    int count() override {
        m_rows.assign(Session::get().m_groupClaims.begin(), Session::get().m_groupClaims.end());
        return static_cast<int>(m_rows.size());
    }
    void row(int i, float y) override {
        addText(fmt::format("Group {}  ->  {}", m_rows[i].first, m_rows[i].second), {m_size.width / 2, y}, .5f, "chatFont.fnt", {220, 220, 255});
    }
};

// ---------------------------------------------------------------- leaderboard
class LeaderboardPopup : public ListPopup {
public:
    static LeaderboardPopup* create() { auto r = new LeaderboardPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    bool initPopup() {
        if (!setupList(420, 290, "Leaderboard", 7, 55)) return false;
        m_rowH = 28;
        Session::get().requestLeaderboard();
        rebuild();
        return true;
    }
    int count() override { return static_cast<int>(Session::get().m_leaderboard.size()); }
    void row(int i, float y) override {
        auto const& r = Session::get().m_leaderboard[i];
        ccColor3B medal = i == 0 ? ccColor3B{255, 215, 0} : i == 1 ? ccColor3B{200, 200, 220} : i == 2 ? ccColor3B{220, 140, 80} : ccColor3B{255, 255, 255};
        addText(fmt::format("{}.", i + 1), {40, y}, .6f, "goldFont.fnt", medal);
        addText(r.name, {60, y + 5}, .5f, "chatFont.fnt", Session::get().colorForUid(r.uid), {0, .5f}, 150);
        int mins = static_cast<int>(r.timeMs / 60000);
        addText(fmt::format("{} in level  |  {} placed  |  {} deleted  |  {}h {}m  |  {} achievements",
            r.objectsNow, r.placed, r.deleted, mins / 60, mins % 60, r.ach), {60, y - 7}, .4f, "chatFont.fnt", {190, 190, 190}, {0, .5f}, 320);
    }
};

// ---------------------------------------------------------------- timelapse
class ReplayPopup : public BasePopup {
public:
    static ReplayPopup* create() { auto r = new ReplayPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
    ~ReplayPopup() override { Session::get().onData = nullptr; }
protected:
    CCDrawNode* m_draw = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::unordered_map<std::string, std::pair<CCPoint, ccColor3B>> m_live;
    std::unordered_map<std::string, CCPoint> m_known;
    size_t m_frame = 0;
    float m_minX = 0, m_minY = 0, m_maxX = 1, m_maxY = 1;
    bool m_playing = false;
    float m_speed = 1.f;

    bool initPopup() {
        if (!setup(460, 300, "Timelapse")) return false;
        auto clip = CCClippingNode::create();
        auto stencil = CCLayerColor::create({0, 0, 0, 255}, m_size.width - 40, m_size.height - 110);
        clip->setStencil(stencil);
        clip->setPosition({20, 60});
        auto bg = CCLayerColor::create({0, 0, 0, 120}, m_size.width - 40, m_size.height - 110);
        clip->addChild(bg);
        m_draw = CCDrawNode::create();
        clip->addChild(m_draw);
        m_mainLayer->addChild(clip, 5);
        m_status = text("Loading the build history...", {m_size.width / 2, m_size.height - 40}, .45f, "chatFont.fnt");
        button("Play", {m_size.width / 2 - 110, 30}, [this](auto) { start(); }, .5f);
        button("Speed x2", {m_size.width / 2, 30}, [this](auto) { m_speed = m_speed >= 8 ? 1 : m_speed * 2; }, .5f);
        button("Skip to End", {m_size.width / 2 + 110, 30}, [this](auto) { while (step()) {} redraw(); }, .5f);
        Session::get().onData = [this] {
            if (Session::get().m_replayDone) {
                m_status->setString(fmt::format("{} edits recorded - press Play", Session::get().m_replay.size()).c_str());
                computeBounds();
            }
        };
        Session::get().requestReplay(0);
        this->schedule(schedule_selector(ReplayPopup::tick), 1.f / 30.f);
        return true;
    }
    void computeBounds() {
        bool first = true;
        for (auto const& f : Session::get().m_replay) for (auto const& op : f.ops) {
            if (!op.hasPos) continue;
            if (first) { m_minX = m_maxX = op.x; m_minY = m_maxY = op.y; first = false; }
            m_minX = std::min(m_minX, op.x); m_maxX = std::max(m_maxX, op.x);
            m_minY = std::min(m_minY, op.y); m_maxY = std::max(m_maxY, op.y);
        }
        if (m_maxX - m_minX < 30) m_maxX = m_minX + 30;
        if (m_maxY - m_minY < 30) m_maxY = m_minY + 30;
    }
    void start() {
        if (!Session::get().m_replayDone) return;
        m_live.clear(); m_known.clear(); m_frame = 0; m_playing = true;
    }
    bool step() {
        auto& rp = Session::get().m_replay;
        if (m_frame >= rp.size()) { m_playing = false; return false; }
        auto const& f = rp[m_frame++];
        auto col = Session::get().colorForUid(f.author);
        for (auto const& op : f.ops) {
            if (op.o == 2) { m_live.erase(op.id); continue; }
            CCPoint p = op.hasPos ? CCPoint(op.x, op.y) : (m_known.count(op.id) ? m_known[op.id] : CCPoint(0, 0));
            m_known[op.id] = p;
            m_live[op.id] = {p, op.o == 0 ? col : (m_live.count(op.id) ? m_live[op.id].second : col)};
        }
        return true;
    }
    void redraw() {
        m_draw->clear();
        float w = m_size.width - 40, h = m_size.height - 110;
        float sx = w / (m_maxX - m_minX), sy = h / (m_maxY - m_minY);
        float sc = std::min(sx, sy);
        size_t n = 0;
        for (auto const& [id, v] : m_live) {
            if (++n > 20000) break;
            CCPoint p((v.first.x - m_minX) * sc + 4, (v.first.y - m_minY) * sc + 4);
            m_draw->drawDot(p, std::max(1.f, 15.f * sc), c4f(v.second, .9f));
        }
        m_status->setString(fmt::format("edit {}/{}  |  {} objects  |  speed x{}", m_frame, Session::get().m_replay.size(), m_live.size(), static_cast<int>(m_speed)).c_str());
    }
    void tick(float) {
        if (!m_playing) return;
        auto total = Session::get().m_replay.size();
        // a full timelapse takes ~20 seconds at x1
        size_t per = std::max<size_t>(1, static_cast<size_t>(total / 600.f * m_speed));
        for (size_t i = 0; i < per; i++) if (!step()) break;
        redraw();
    }
};

// ---------------------------------------------------------------- tools hub
class ToolsPopup : public BasePopup {
public:
    static ToolsPopup* create() { auto r = new ToolsPopup(); if (r->initPopup()) { r->autorelease(); return r; } delete r; return nullptr; }
protected:
    bool initPopup() {
        if (!setup(440, 300, "Collab Tools")) return false;
        auto& s = Session::get();
        float cx = m_size.width / 2, top = m_size.height;
        auto grid = [&](std::vector<std::pair<char const*, std::function<void()>>> items, float y) {
            float w = (m_size.width - 40) / items.size();
            for (size_t i = 0; i < items.size(); i++) {
                auto fn = items[i].second;
                button(items[i].first, {20 + w * (i + .5f), y}, [fn](auto) { fn(); }, .42f, "GJ_button_04.png");
            }
        };
        grid({
            {"Timeline", [this] { onClose(nullptr); if (auto p = TimelinePopup::create()) p->show(); }},
            {"Tasks", [this] { onClose(nullptr); if (auto p = TasksPopup::create()) p->show(); }},
            {"Votes", [this] { onClose(nullptr); if (auto p = VotesPopup::create()) p->show(); }},
            {"Templates", [this] { onClose(nullptr); if (auto p = TemplatesPopup::create()) p->show(); }},
        }, top - 55);
        grid({
            {"Group IDs", [this] { onClose(nullptr); if (auto p = GroupsPopup::create()) p->show(); }},
            {"Leaderboard", [this] { onClose(nullptr); if (auto p = LeaderboardPopup::create()) p->show(); }},
            {"Timelapse", [this] { onClose(nullptr); if (auto p = ReplayPopup::create()) p->show(); }},
            {"Credits Text", [] { Session::get().buildCreditsObject(); }},
        }, top - 88);

        toggle("Heatmap (who built what)", {30, top - 122}, s.m_heatmap, [](bool on) { Session::get().m_heatmap = on; });
        toggle("Draw mode (sketch on the level)", {230, top - 122}, s.m_drawMode, [](bool on) {
            Session::get().m_drawMode = on;
            if (on) Notification::create("Draw mode ON - drag to sketch. Turn it off to edit again.", NotificationIcon::Info, 3.f)->show();
        });
        grid({
            {"Clear My Drawings", [] { Session::get().clearDrawings(true); }},
            {"Undo My Last", [] { Session::get().undoUser(Session::get().m_myPid, 1); }},
            {"Undo My Last 10", [] { Session::get().undoUser(Session::get().m_myPid, 10); }},
        }, top - 152);
        if (s.m_myRole == "host") {
            grid({
                {"Clear All Drawings", [] { Session::get().clearDrawings(false); }},
                {"Timer Start", [] { Session::get().timerOp("start"); }},
                {"Timer Stop", [] { Session::get().timerOp("stop"); }},
                {"Timer Reset", [] { Session::get().timerOp("reset"); }},
            }, top - 185);
        } else {
            text("The host controls the shared stopwatch", {cx, top - 185}, .4f, "chatFont.fnt", {170, 170, 170});
        }

        text("Reactions", {cx, 72}, .45f, "goldFont.fnt");
        std::vector<char const*> reacts = {"GG", "W", "L", "LOL", "!!", "??", "<3", "FIRE", "WOW"};
        float w = (m_size.width - 40) / reacts.size();
        for (size_t i = 0; i < reacts.size(); i++) {
            std::string r = reacts[i];
            button(reacts[i], {20 + w * (i + .5f), 45}, [r](auto) { Session::get().sendReaction(r); }, .45f, "GJ_button_05.png");
        }
        return true;
    }
};

void openToolsPopup() {
    if (!Session::get().inRoom()) {
        Notification::create("Join or host a room first", NotificationIcon::Info)->show();
        return;
    }
    if (auto p = ToolsPopup::create()) p->show();
}

}
