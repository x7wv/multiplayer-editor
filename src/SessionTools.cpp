// Multiplayer Editor — made by x7wv
//
// Collab tools: timeline, per-player undo, tasks, votes, stopwatch, group IDs,
// templates, leaderboard, timelapse, reactions, draw mode, heatmap colors,
// auto collab-credits and color channel sync.
#include "Session.hpp"
#include "Json.hpp"
#include "Net.hpp"
#include <Geode/utils/base64.hpp>

using namespace geode::prelude;

namespace me {

// ---------------------------------------------------------------- parsing
static void parseTasks(std::vector<Task>& out, matjson::Value const& arr) {
    out.clear();
    if (!arr.isArray()) return;
    for (auto const& t : arr) {
        Task task;
        task.id = jstr(t, "id"); task.text = jstr(t, "text"); task.by = jstr(t, "by");
        task.claim = t.contains("claim") && t["claim"].isString() ? jstr(t, "claim") : "";
        task.done = jbool(t, "done");
        if (jarr(t, "rect")) for (size_t i = 0; i < 4 && i < t["rect"].size(); i++) {
            auto r = t["rect"][i].asDouble();
            task.rect[i] = r ? static_cast<float>(r.unwrap()) : 0.f;
        }
        out.push_back(task);
    }
}

static int countOrLen(matjson::Value const& v, std::string_view k) {
    if (!v.contains(k)) return 0;
    if (v[k].isArray()) return static_cast<int>(v[k].size());
    return jint(v, k);
}

static void parseVotes(std::vector<Vote>& out, matjson::Value const& arr) {
    out.clear();
    if (!arr.isArray()) return;
    for (auto const& v : arr) {
        Vote vote;
        vote.id = jstr(v, "id"); vote.q = jstr(v, "q"); vote.by = jstr(v, "by");
        vote.yes = countOrLen(v, "yes"); vote.no = countOrLen(v, "no");
        vote.open = jbool(v, "open", true);
        out.push_back(vote);
    }
}

static void parseTemplates(std::vector<TemplateInfo>& out, matjson::Value const& arr) {
    out.clear();
    if (!arr.isArray()) return;
    for (auto const& t : arr) out.push_back({jstr(t, "id"), jstr(t, "name"), jstr(t, "by"), jint(t, "count")});
}

static Drawing parseDrawing(matjson::Value const& d) {
    Drawing dr;
    dr.id = jstr(d, "id"); dr.uid = jstr(d, "uid");
    dr.color = jcolor(d, "c");
    dr.width = static_cast<float>(jnum(d, "w", 3));
    if (jarr(d, "pts")) {
        auto const& p = d["pts"];
        for (size_t i = 0; i + 1 < p.size(); i += 2) {
            auto x = p[i].asDouble(), y = p[i + 1].asDouble();
            if (x && y) dr.pts.emplace_back(static_cast<float>(x.unwrap()), static_cast<float>(y.unwrap()));
        }
    }
    return dr;
}

void Session::readExtras(matjson::Value const& m) {
    if (m.contains("tasks")) parseTasks(m_tasks, m["tasks"]);
    if (m.contains("votes")) parseVotes(m_votes, m["votes"]);
    if (m.contains("tpls")) parseTemplates(m_templates, m["tpls"]);
    if (jarr(m, "drawings")) {
        m_drawings.clear();
        for (auto const& d : m["drawings"]) m_drawings.push_back(parseDrawing(d));
    }
    if (m.contains("timer") && m["timer"].isObject()) {
        auto const& tm = m["timer"];
        m_timerRunning = jbool(tm, "running");
        m_timerStart = jnum(tm, "start");
        m_timerAcc = jnum(tm, "acc");
        if (m.contains("now")) m_clockOffset = jnum(m, "now") - wallMs();
    }
}

void Session::handleExtra(std::string const& t, matjson::Value const& m) {
    bool changed = true;
    if (t == "history") {
        if (!jbool(m, "append")) {}
        auto list = m["list"];
        if (list.isArray()) {
            for (auto const& e : list) {
                HistoryEntry h;
                h.q = jint(e, "q"); h.t = jnum(e, "t"); h.name = jstr(e, "name");
                h.tag = e.contains("tag") && e["tag"].isString() ? jstr(e, "tag") : "";
                h.add = jint(e, "add"); h.del = jint(e, "del"); h.upd = jint(e, "upd");
                bool dup = std::any_of(m_history.begin(), m_history.end(), [&](auto const& x) { return x.q == h.q; });
                if (!dup) m_history.push_back(h);
            }
            std::sort(m_history.begin(), m_history.end(), [](auto const& a, auto const& b) { return a.q > b.q; });
        }
    } else if (t == "tasks") {
        parseTasks(m_tasks, m["tasks"]);
    } else if (t == "votes") {
        parseVotes(m_votes, m["votes"]);
    } else if (t == "timer") {
        readExtras(m);
    } else if (t == "tpls") {
        parseTemplates(m_templates, m["list"]);
    } else if (t == "tplData") {
        if (!m_lel || !m_lel->m_editorUI) return;
        auto s = jstr(m, "s");
        if (s.empty()) return;
        auto arr = m_lel->m_editorUI->pasteObjects(gd::string(s), true, false);
        if (arr) for (size_t i = 0; i < arr->count(); i++) onLocalTouched(static_cast<GameObject*>(arr->objectAtIndex(i)));
        Notification::create("Pasted template: " + jstr(m, "name"), NotificationIcon::Success)->show();
    } else if (t == "groupsAlloc") {
        m_lastGroups.clear();
        if (jarr(m, "ids")) for (auto const& g : m["ids"]) if (auto v = g.asDouble()) m_lastGroups.push_back(static_cast<int>(v.unwrap()));
        std::string list;
        for (auto g : m_lastGroups) list += (list.empty() ? "" : ", ") + std::to_string(g);
        if (!list.empty()) {
            geode::utils::clipboard::write(list);
            Notification::create("Your free group IDs: " + list + " (copied)", NotificationIcon::Success, 4.f)->show();
        }
    } else if (t == "groupClaims") {
        m_groupClaims.clear();
        if (m.contains("claims") && m["claims"].isObject()) {
            for (auto const& [g, name] : m["claims"]) {
                auto n = name.asString();
                m_groupClaims[std::atoi(g.c_str())] = n ? n.unwrap() : "?";
            }
        }
    } else if (t == "leaderboard") {
        m_leaderboard.clear();
        if (jarr(m, "list")) for (auto const& r : m["list"]) {
            LeaderRow row;
            row.uid = jstr(r, "uid"); row.name = jstr(r, "name");
            row.placed = jint(r, "placed"); row.deleted = jint(r, "deleted"); row.chats = jint(r, "chats");
            row.objectsNow = jint(r, "objectsNow"); row.ach = jint(r, "ach"); row.timeMs = jnum(r, "timeMs");
            m_leaderboard.push_back(row);
        }
    } else if (t == "replay") {
        int last = 0;
        if (jarr(m, "list")) for (auto const& e : m["list"]) {
            ReplayFrame f;
            f.author = jstr(e, "a");
            last = jint(e, "q");
            if (jarr(e, "ops")) for (auto const& op : e["ops"]) {
                ReplayOp r;
                auto o = jstr(op, "o");
                r.o = o == "add" ? 0 : o == "upd" ? 1 : 2;
                r.id = jstr(op, "id");
                auto s = jstr(op, "s");
                if (!s.empty()) {
                    // save strings are "key,value,..." - key 2 is x, key 3 is y
                    auto parts = utils::string::split(s, ",");
                    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                        if (parts[i] == "2") { r.x = std::strtof(parts[i + 1].c_str(), nullptr); r.hasPos = true; }
                        else if (parts[i] == "3") r.y = std::strtof(parts[i + 1].c_str(), nullptr);
                    }
                }
                f.ops.push_back(r);
            }
            m_replay.push_back(std::move(f));
        }
        m_replayDone = jbool(m, "done") || m_replay.size() > 30000;
        if (!m_replayDone) requestReplay(last);
    } else if (t == "react") {
        auto it = m_players.find(jint(m, "p"));
        ccColor3B col = it != m_players.end() ? it->second.color : ccColor3B{255, 255, 255};
        m_reactions.push_back({jstr(m, "e"), CCPoint(static_cast<float>(jnum(m, "x")), static_cast<float>(jnum(m, "y"))), col, 0.f});
    } else if (t == "draw") {
        m_drawings.push_back(parseDrawing(m["d"]));
        if (m_drawings.size() > 500) m_drawings.erase(m_drawings.begin());
    } else if (t == "drawSet") {
        m_drawings.clear();
        if (jarr(m, "drawings")) for (auto const& d : m["drawings"]) m_drawings.push_back(parseDrawing(d));
    } else if (t == "credits") {
        // handled by buildCreditsObject via m_authorOf; nothing to store
        changed = false;
    } else {
        changed = false;
    }
    if (changed && onData) onData();
}

// ---------------------------------------------------------------- actions
void Session::requestHistory(int before) {
    if (!before) m_history.clear();
    auto msg = matjson::makeObject({{"t", "history"}, {"limit", 200}});
    if (before) msg["before"] = before;
    sendRaw(msg);
}
void Session::restore(int seq) { sendRaw(matjson::makeObject({{"t", "restore"}, {"seq", seq}})); }
void Session::undoUser(int pid, int n) { sendRaw(matjson::makeObject({{"t", "undoUser"}, {"pid", pid}, {"n", n}})); }

void Session::taskOp(std::string const& op, std::string const& id, std::string const& text) {
    auto msg = matjson::makeObject({{"t", "task"}, {"op", op}});
    if (!id.empty()) msg["id"] = id;
    if (op == "add") {
        auto r = viewRect();
        auto rect = matjson::Value::array();
        rect.push(r.getMinX()); rect.push(r.getMinY()); rect.push(r.getMaxX()); rect.push(r.getMaxY());
        msg["rect"] = rect;
        msg["text"] = text;
    }
    sendRaw(msg);
}
void Session::voteOp(std::string const& op, std::string const& id, int v, std::string const& q) {
    auto msg = matjson::makeObject({{"t", "vote"}, {"op", op}});
    if (!id.empty()) msg["id"] = id;
    if (op == "cast") msg["v"] = v;
    if (op == "new") msg["q"] = q;
    sendRaw(msg);
}
void Session::timerOp(std::string const& op) { sendRaw(matjson::makeObject({{"t", "timer"}, {"op", op}})); }
void Session::requestGroups(int n) {
    sendRaw(matjson::makeObject({{"t", "groups"}, {"n", n}}));
}
void Session::requestTemplates() { sendRaw(matjson::makeObject({{"t", "tpl"}, {"op", "list"}})); }
void Session::deleteTemplate(std::string const& id) { sendRaw(matjson::makeObject({{"t", "tpl"}, {"op", "del"}, {"id", id}})); }
void Session::pasteTemplate(std::string const& id) { sendRaw(matjson::makeObject({{"t", "tpl"}, {"op", "get"}, {"id", id}})); }

void Session::saveTemplate(std::string const& name) {
    if (!m_lel || !m_lel->m_editorUI) return;
    auto ui = m_lel->m_editorUI;
    auto sel = ui->getSelectedObjects();
    if (!sel || sel->count() == 0) {
        Notification::create("Select the objects you want to save first", NotificationIcon::Info)->show();
        return;
    }
    std::string s = ui->copyObjects(sel, true, true);
    sendRaw(matjson::makeObject({
        {"t", "tpl"}, {"op", "save"}, {"name", name.empty() ? std::string("Template") : name},
        {"s", s}, {"count", static_cast<int>(sel->count())},
    }));
    Notification::create("Saved template to the room", NotificationIcon::Success)->show();
}

void Session::requestLeaderboard() { sendRaw(matjson::makeObject({{"t", "leaderboard"}})); }
void Session::requestReplay(int from) {
    if (!from) { m_replay.clear(); m_replayDone = false; }
    sendRaw(matjson::makeObject({{"t", "replay"}, {"from", from}}));
}

void Session::sendReaction(std::string const& text) {
    if (!m_lel || !m_lel->m_objectLayer) return;
    CCPoint screen;
#ifdef GEODE_IS_DESKTOP
    screen = geode::cocos::getMousePos();
#else
    auto win = CCDirector::get()->getWinSize();
    screen = CCPoint(win.width / 2, win.height / 2);
#endif
    auto world = m_lel->m_objectLayer->convertToNodeSpace(screen);
    sendRaw(matjson::makeObject({{"t", "react"}, {"e", text}, {"x", world.x}, {"y", world.y}}));
}

void Session::sendDrawing(std::vector<CCPoint> const& pts) {
    if (pts.size() < 2) return;
    auto arr = matjson::Value::array();
    for (auto& p : pts) { arr.push(std::round(p.x * 10) / 10); arr.push(std::round(p.y * 10) / 10); }
    auto it = m_players.find(m_myPid);
    ccColor3B col = it != m_players.end() ? it->second.color : ccColor3B{0, 230, 255};
    sendRaw(matjson::makeObject({{"t", "draw"}, {"pts", arr}, {"c", colorJson(col)}, {"w", 3}}));
}
void Session::clearDrawings(bool mine) { sendRaw(matjson::makeObject({{"t", "drawClear"}, {"mine", mine}})); }

double Session::timerSeconds() const {
    double ms = m_timerAcc;
    if (m_timerRunning) ms += (wallMs() + m_clockOffset) - m_timerStart;
    return std::max(0.0, ms / 1000.0);
}

ccColor3B Session::colorForUid(std::string const& uid) const {
    for (auto& [_, p] : m_players) if (p.uid == uid) return p.color;
    // stable color from the uid for people who left
    uint32_t h = 2166136261u;
    for (char c : uid) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
    float hue = static_cast<float>(h % 360);
    auto hsv = [&](float hdeg) -> ccColor3B {
        float c = 0.85f, x = c * (1 - std::fabs(std::fmod(hdeg / 60.f, 2.f) - 1)), m = 0.15f;
        float r = 0, g = 0, b = 0;
        if (hdeg < 60) { r = c; g = x; } else if (hdeg < 120) { r = x; g = c; } else if (hdeg < 180) { g = c; b = x; }
        else if (hdeg < 240) { g = x; b = c; } else if (hdeg < 300) { r = x; b = c; } else { r = c; b = x; }
        return {static_cast<GLubyte>((r + m) * 255), static_cast<GLubyte>((g + m) * 255), static_cast<GLubyte>((b + m) * 255)};
    };
    return hsv(hue);
}

CCRect Session::viewRect() const {
    if (!m_lel || !m_lel->m_objectLayer) return {};
    auto layer = m_lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    auto a = layer->convertToNodeSpace(CCPoint(0, 0));
    auto b = layer->convertToNodeSpace(CCPoint(win.width, win.height));
    return CCRect(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
}

void Session::goTo(CCPoint world) {
    if (!m_lel || !m_lel->m_objectLayer) return;
    m_followPid = 0;
    auto layer = m_lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    layer->setPosition(CCPoint(win.width / 2, win.height / 2) - world * layer->getScale());
}

// Builds text objects listing who built what, at the middle of the screen.
void Session::buildCreditsObject() {
    if (!m_lel || !m_lel->m_objectLayer) return;
    std::map<std::string, int> counts;
    for (auto& [obj, rec] : m_objToId) {
        auto it = m_authorOf.find(rec.id);
        counts[it != m_authorOf.end() ? it->second : m_uid]++;
    }
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    int total = 0;
    for (auto& [_, n] : sorted) total += n;
    if (!total) return;

    std::vector<std::string> lines = {"COLLAB CREDITS"};
    std::string hostName;
    for (auto& [_, p] : m_players) if (p.role == "host") hostName = p.display();
    if (!hostName.empty()) lines.push_back("Hosted by " + hostName);
    for (auto& [uid, n] : sorted) {
        auto nm = m_names.count(uid) ? m_names.at(uid) : (uid == "timeline" ? "Timeline" : "Unknown");
        lines.push_back(fmt::format("{} - {}%", nm, std::max(1, n * 100 / total)));
        if (lines.size() > 20) break;
    }
    lines.push_back("Built with Multiplayer Editor by x7wv");

    auto win = CCDirector::get()->getWinSize();
    auto center = m_lel->m_objectLayer->convertToNodeSpace(CCPoint(win.width / 2, win.height / 2));
    std::string all;
    float y = center.y + lines.size() * 15.f;
    for (auto& line : lines) {
        auto b64 = geode::utils::base64::encode(line);
        all += fmt::format("1,914,2,{:.1f},3,{:.1f},31,{};", center.x, y, b64);
        y -= 30.f;
    }
    // created as a normal local edit, so it syncs to everyone
    auto arr = m_lel->createObjectsFromString(gd::string(all), false, true);
    if (arr) for (size_t i = 0; i < arr->count(); i++) onLocalTouched(static_cast<GameObject*>(arr->objectAtIndex(i)));
    Notification::create("Added collab credits to the level!", NotificationIcon::Success)->show();
}

// ---------------------------------------------------------------- color channels
// The level settings string holds colors under kS38 as "1_r_2_g_3_b_..._6_channel|...".
void Session::applyColors(std::string const& ls) {
    if (!m_lel || !m_lel->m_levelSettings) return;
    auto em = m_lel->m_levelSettings->m_effectManager;
    if (!em) return;
    auto parts = utils::string::split(ls, ",");
    std::string colors;
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        if (parts[i] == "kS38") { colors = parts[i + 1]; break; }
    }
    if (colors.empty()) return;
    bool any = false;
    for (auto& entry : utils::string::split(colors, "|")) {
        if (entry.empty()) continue;
        auto kv = utils::string::split(entry, "_");
        int channel = -1;
        for (size_t i = 0; i + 1 < kv.size(); i += 2) if (kv[i] == "6") channel = std::atoi(kv[i + 1].c_str());
        if (channel <= 0) continue;
        if (auto action = em->getColorAction(channel)) {
            action->setupFromString(gd::string(entry));
            any = true;
        }
    }
    if (any) m_lel->updateLevelColors();
}

}
