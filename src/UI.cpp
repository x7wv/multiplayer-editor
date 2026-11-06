// Multiplayer Editor — made by x7wv
#include "UI.hpp"
#include "Common.hpp"
#include "Session.hpp"
#include "Net.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/general.hpp>

using namespace geode::prelude;
using geode::cocos::CCMenuItemExt;

namespace me {

// ---------------------------------------------------------------- themes
Theme const& theme() {
    static Theme x7wv{{170, 90, 255}, {0, 230, 255}, {255, 255, 255}, {20, 8, 40, 170}};
    static Theme classic{{255, 200, 0}, {120, 255, 120}, {255, 255, 255}, {0, 0, 0, 140}};
    static Theme neon{{255, 0, 200}, {0, 255, 150}, {255, 255, 255}, {10, 0, 20, 170}};
    static Theme midnight{{80, 140, 255}, {160, 200, 255}, {220, 230, 255}, {5, 10, 30, 190}};
    static Theme sunset{{255, 120, 60}, {255, 210, 90}, {255, 245, 230}, {40, 10, 10, 170}};
    auto t = Mod::get()->getSettingValue<std::string>("theme");
    if (t == "Classic") return classic;
    if (t == "Neon") return neon;
    if (t == "Midnight") return midnight;
    if (t == "Sunset") return sunset;
    return x7wv;
}

// ---------------------------------------------------------------- overlay
Overlay* Overlay::create(LevelEditorLayer* lel) {
    auto ret = new Overlay();
    if (ret->initOverlay(lel)) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

bool Overlay::initOverlay(LevelEditorLayer* lel) {
    if (!CCNode::init()) return false;
    m_lel = lel;
    auto win = CCDirector::get()->getWinSize();
    this->setContentSize(win);
    this->setID("multiplayer-editor-overlay"_spr);

    m_draw = CCDrawNode::create();
    this->addChild(m_draw, 0);
    m_markerNode = CCNode::create();
    this->addChild(m_markerNode, 1);
    m_toolLabels = CCNode::create();
    this->addChild(m_toolLabels, 2);
    m_heatLegend = CCNode::create();
    m_heatLegend->setPosition({win.width - 8, win.height - 95});
    this->addChild(m_heatLegend, 10);
    m_timer = label("", "bigFont.fnt", .45f, {255, 255, 255});
    m_timer->setAnchorPoint({.5f, 1.f});
    m_timer->setPosition({win.width / 2, win.height - 74});
    this->addChild(m_timer, 10);

    m_status = label("", "chatFont.fnt", .5f, theme().accent2);
    m_status->setAnchorPoint({.5f, 1.f});
    m_status->setPosition({win.width / 2, win.height - 28});
    this->addChild(m_status, 10);

    m_credit = label("Multiplayer Editor by x7wv", "chatFont.fnt", .35f, theme().accent);
    m_credit->setAnchorPoint({.5f, 1.f});
    m_credit->setOpacity(160);
    m_credit->setPosition({win.width / 2, win.height - 40});
    this->addChild(m_credit, 10);

    m_feedNode = CCNode::create();
    m_feedNode->setPosition({8, win.height - 95});
    this->addChild(m_feedNode, 10);

    m_chatNode = CCNode::create();
    m_chatNode->setPosition({8, 100});
    this->addChild(m_chatNode, 10);

    m_typing = label("", "chatFont.fnt", .4f, {200, 200, 200});
    m_typing->setAnchorPoint({0, 0});
    m_typing->setPosition({8, 88});
    this->addChild(m_typing, 10);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    m_menu->setContentSize(win);
    this->addChild(m_menu, 20);

    // main button: always visible
    auto mainSpr = CircleButtonSprite::create(label("MP", "bigFont.fnt", .6f, theme().accent2), CircleBaseColor::DarkPurple, CircleBaseSize::Small);
    mainSpr->setScale(.8f);
    auto mainBtn = CCMenuItemExt::createSpriteExtra(mainSpr, [](auto) { openMainPopup(); });
    mainBtn->setPosition({win.width / 2 - 95, win.height - 62});
    mainBtn->setID("main-button"_spr);
    m_menu->addChild(mainBtn);

    m_roomButtons = CCNode::create();
    m_menu->addChild(m_roomButtons);
    float x = win.width / 2 - 55;
    auto add = [&](char const* text, geode::Function<void(CCMenuItemSpriteExtra*)> cb) {
        auto b = textButton(text, std::move(cb), .45f, "GJ_button_04.png");
        b->setPosition({x, win.height - 62});
        x += b->getScaledContentSize().width + 4;
        m_menu->addChild(b);
        b->setTag(77);
    };
    add("Chat", [](auto) { openChatPopup(); });
    add("Players", [](auto) { openPlayersPopup(); });
    add("Ping", [this](auto) {
        auto win = CCDirector::get()->getWinSize();
        auto world = m_lel->m_objectLayer->convertToNodeSpace(CCPoint{win.width / 2, win.height / 2});
        Session::get().sendPing(world, "look here!");
    });
    add("Tools", [](auto) { openToolsPopup(); });
    add("Unfollow", [](auto) { Session::get().followPlayer(0); });

    this->refreshButtons();
    this->scheduleUpdate();
    return true;
}

void Overlay::refreshButtons() {
    bool in = Session::get().inRoom();
    for (auto child : CCArrayExt<CCNode*>(m_menu->getChildren())) {
        if (child->getTag() == 77) child->setVisible(in);
    }
}

CCPoint Overlay::toScreen(CCPoint world) {
    auto layer = m_lel->m_objectLayer;
    return this->convertToNodeSpace(layer->convertToWorldSpace(world));
}

void Overlay::update(float dt) {
    auto& s = Session::get();
    s.update(dt);
    this->refreshButtons();

    m_statusTimer += dt;
    if (m_statusTimer > .25f) {
        m_statusTimer = 0;
        std::string st;
        if (s.inRoom()) {
            st = fmt::format("{}  {}  |  {} online  |  {}ms  |  {}{}{}",
                s.online() ? "ONLINE" : "RECONNECTING...", s.m_code, s.m_players.size(), s.m_rtt,
                s.m_myRole, s.m_frozen ? "  |  FROZEN" : "", s.m_finished ? "  |  FINISHED" : "");
        } else if (s.m_pendingApproval) {
            st = "Waiting for the host to accept you...";
        }
        m_status->setString(st.c_str());
        m_credit->setVisible(s.inRoom());
        if (!s.m_joinRequests.empty()) {
            m_status->setString((st + fmt::format("  |  {} join request(s)", s.m_joinRequests.size())).c_str());
        }
    }

    m_draw->clear();
    if (!s.inRoom()) {
        for (auto& [_, n] : m_cursors) n->removeFromParent();
        m_cursors.clear();
        for (auto& [_, g] : m_ghosts) g->removeFromParent();
        m_ghosts.clear();
        m_chatNode->removeAllChildren();
        m_feedNode->removeAllChildren();
        m_typing->setString("");
        m_toolLabels->removeAllChildren();
        m_heatLegend->removeAllChildren();
        m_timer->setString("");
        return;
    }
    if (Mod::get()->getSettingValue<bool>("show-selections")) drawSelections();
    updateCursors();
    updateGhosts();
    updateMarkers();
    drawTools();
    m_chatAge += dt;
    if (m_chatAge >= .1f) { m_chatAge = 0; updateChat(); updateToolLabels(); }
    while (s.m_confettiPending > 0) { s.m_confettiPending--; spawnConfetti(); }
}

void Overlay::drawSelections() {
    auto& s = Session::get();
    for (auto& [pid, p] : s.m_players) {
        if (pid == s.m_myPid || p.selection.empty()) continue;
        size_t drawn = 0;
        for (auto& id : p.selection) {
            if (drawn++ > 300) break;
            auto obj = s.objectFor(id);
            if (!obj) continue;
            auto r = obj->boundingBox();
            CCPoint a = toScreen({r.getMinX(), r.getMinY()}), b = toScreen({r.getMaxX(), r.getMaxY()});
            CCPoint verts[4] = {a, {b.x, a.y}, b, {a.x, b.y}};
            m_draw->drawPolygon(verts, 4, c4f(p.color, .12f), 1.f, c4f(p.color, .9f));
        }
    }
    // locked objects get a red outline
    size_t drawn = 0;
    for (auto& id : s.m_locks) {
        if (drawn++ > 300) break;
        auto obj = s.objectFor(id);
        if (!obj) continue;
        auto r = obj->boundingBox();
        CCPoint a = toScreen({r.getMinX(), r.getMinY()}), b = toScreen({r.getMaxX(), r.getMaxY()});
        CCPoint verts[4] = {a, {b.x, a.y}, b, {a.x, b.y}};
        m_draw->drawPolygon(verts, 4, {1, 0, 0, .05f}, .6f, {1, .2f, .2f, .7f});
    }
}

void Overlay::updateCursors() {
    auto& s = Session::get();
    bool show = Mod::get()->getSettingValue<bool>("show-cursors");
    std::vector<int> gone;
    for (auto& [pid, node] : m_cursors) if (!s.m_players.count(pid)) gone.push_back(pid);
    for (int pid : gone) { m_cursors[pid]->removeFromParent(); m_cursors.erase(pid); }

    for (auto& [pid, p] : s.m_players) {
        if (pid == s.m_myPid) continue;
        auto& node = m_cursors[pid];
        if (!node) {
            node = CCNode::create();
            auto arrow = CCDrawNode::create();
            CCPoint tri[3] = {{0, 0}, {4, -12}, {11, -7}};
            arrow->drawPolygon(tri, 3, c4f(p.color), 1.f, {0, 0, 0, 1});
            arrow->setTag(1);
            node->addChild(arrow);
            auto name = label(p.display(), "chatFont.fnt", .45f, p.color);
            name->setAnchorPoint({0, 1});
            name->setPosition({10, -10});
            name->setTag(2);
            node->addChild(name);
            this->addChild(node, 5);
        }
        node->setVisible(show && p.hasCursor);
        auto target = toScreen(p.cursor);
        auto cur = node->getPosition();
        // smooth movement between network updates
        node->setPosition(cur.getDistance(target) > 400 ? target : cur + (target - cur) * .35f);
        if (auto name = static_cast<CCLabelBMFont*>(node->getChildByTag(2))) {
            std::string text = p.display();
            if (p.afk) text += " (afk)";
            if (p.playing) text += " (playtesting)";
            if (p.typing) text += " ...";
            if (text != name->getString()) name->setString(text.c_str());
            name->setColor(p.color);
        }
        for (auto c : CCArrayExt<CCNode*>(node->getChildren())) if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(c)) rgba->setOpacity(p.afk ? 90 : 255);
    }
}

void Overlay::updateGhosts() {
    auto& s = Session::get();
    std::vector<int> gone;
    for (auto& [pid, g] : m_ghosts) {
        auto it = s.m_players.find(pid);
        if (it == s.m_players.end() || !it->second.playOn) gone.push_back(pid);
    }
    for (int pid : gone) { m_ghosts[pid]->removeFromParent(); m_ghosts.erase(pid); }

    auto gm = GameManager::get();
    for (auto& [pid, p] : s.m_players) {
        if (pid == s.m_myPid || !p.playOn) continue;
        auto& g = m_ghosts[pid];
        if (!g) {
            g = SimplePlayer::create(p.iconId > 0 ? p.iconId : 1);
            g->setColors(gm->colorForIdx(p.c1), gm->colorForIdx(p.c2));
            if (p.glow) g->setGlowOutline(gm->colorForIdx(p.c2));
            g->setOpacity(170);
            this->addChild(g, 4);
        }
        g->setScale(m_lel->m_objectLayer->getScale() * .9f);
        g->setPosition(toScreen(p.playPos));
        g->setRotation(p.playRot);
    }
}

void Overlay::updateMarkers() {
    auto& s = Session::get();
    m_markerNode->removeAllChildren();
    for (auto& mk : s.m_markers) {
        auto pos = toScreen(mk.pos);
        float pulse = 10.f + 6.f * std::sin(mk.age * 6.f);
        float alpha = std::clamp(1.f - (mk.age - 6.f) / 2.f, 0.f, 1.f);
        m_draw->drawDot(pos, pulse, c4f(mk.color, .35f * alpha));
        m_draw->drawDot(pos, 4.f, c4f(mk.color, alpha));
        auto l = label(mk.name + ": " + mk.label, "chatFont.fnt", .5f, mk.color);
        l->setPosition(pos + CCPoint{0, 22});
        l->setOpacity(static_cast<GLubyte>(255 * alpha));
        m_markerNode->addChild(l);
    }
}

void Overlay::updateChat() {
    auto& s = Session::get();
    int lines = static_cast<int>(Mod::get()->getSettingValue<int64_t>("chat-lines"));
    m_chatNode->removeAllChildren();
    float y = 0;
    int shown = 0;
    for (auto it = s.m_chat.rbegin(); it != s.m_chat.rend() && shown < lines; ++it, ++shown) {
        if (it->age > 12.f) break;
        float alpha = std::clamp(1.f - (it->age - 10.f) / 2.f, 0.f, 1.f);
        std::string text = it->system ? it->text : it->name + ": " + it->text;
        auto l = label(text, "chatFont.fnt", .5f, it->system ? it->color : ccColor3B{255, 255, 255});
        l->setAnchorPoint({0, 0});
        l->setPosition({0, y});
        l->setOpacity(static_cast<GLubyte>(255 * alpha));
        if (!it->system) {
            auto nameLen = it->name.size() + 1;
            for (size_t i = 0; i < nameLen && i < text.size(); i++) {
                if (auto ch = static_cast<CCSprite*>(l->getChildByTag(static_cast<int>(i)))) ch->setColor(it->color);
            }
        }
        m_chatNode->addChild(l);
        y += 12;
    }

    m_feedNode->removeAllChildren();
    float fy = 0;
    for (auto it = s.m_feed.rbegin(); it != s.m_feed.rend(); ++it) {
        float alpha = std::clamp(1.f - (it->age - 4.f) / 2.f, 0.f, 1.f);
        auto l = label(it->text, "chatFont.fnt", .4f, theme().accent2);
        l->setAnchorPoint({0, 1});
        l->setPosition({0, fy});
        l->setOpacity(static_cast<GLubyte>(220 * alpha));
        m_feedNode->addChild(l);
        fy -= 10;
    }

    std::vector<std::string> typers;
    for (auto& [pid, p] : s.m_players) if (pid != s.m_myPid && p.typing) typers.push_back(p.display());
    std::string t;
    if (typers.size() == 1) t = typers[0] + " is typing...";
    else if (typers.size() > 1) t = fmt::format("{} people are typing...", typers.size());
    if (t != m_typing->getString()) m_typing->setString(t.c_str());
}

void Overlay::drawTools() {
    auto& s = Session::get();
    auto view = s.viewRect();
    // heatmap: every object in view tinted by who built it
    if (s.m_heatmap) {
        size_t drawn = 0;
        auto arr = m_lel->m_objects;
        for (size_t i = 0; i < arr->count() && drawn < 4000; i++) {
            auto obj = static_cast<GameObject*>(arr->objectAtIndex(i));
            auto pos = obj->getPosition();
            if (!view.containsPoint(pos)) continue;
            auto id = s.idFor(obj);
            auto it = s.m_authorOf.find(id);
            if (it == s.m_authorOf.end()) continue;
            auto r = obj->boundingBox();
            CCPoint a = toScreen({r.getMinX(), r.getMinY()}), b = toScreen({r.getMaxX(), r.getMaxY()});
            CCPoint verts[4] = {a, {b.x, a.y}, b, {a.x, b.y}};
            m_draw->drawPolygon(verts, 4, c4f(s.colorForUid(it->second), .45f), 0.f, {0, 0, 0, 0});
            drawn++;
        }
    }
    // sketches
    size_t segs = 0;
    for (auto& d : s.m_drawings) {
        for (size_t i = 1; i < d.pts.size() && segs < 6000; i++, segs++) {
            m_draw->drawSegment(toScreen(d.pts[i - 1]), toScreen(d.pts[i]), d.width / 2, c4f(d.color, .9f));
        }
    }
    auto it = s.m_players.find(s.m_myPid);
    ccColor3B myCol = it != s.m_players.end() ? it->second.color : ccColor3B{0, 230, 255};
    for (size_t i = 1; i < s.m_currentStroke.size(); i++) {
        m_draw->drawSegment(toScreen(s.m_currentStroke[i - 1]), toScreen(s.m_currentStroke[i]), 1.5f, c4f(myCol, .9f));
    }
    // build request areas
    for (auto& t : s.m_tasks) {
        if (t.done) continue;
        CCPoint a = toScreen({t.rect[0], t.rect[1]}), b = toScreen({t.rect[2], t.rect[3]});
        CCPoint verts[4] = {a, {b.x, a.y}, b, {a.x, b.y}};
        auto col = t.claim.empty() ? ccColor3B{255, 220, 80} : ccColor3B{120, 200, 255};
        m_draw->drawPolygon(verts, 4, c4f(col, .04f), 1.f, c4f(col, .6f));
    }
    // floating reactions
    m_markerNode->setVisible(true);
    for (auto& r : s.m_reactions) {
        auto l = label(r.text, "bigFont.fnt", .6f + r.age * .1f, r.color);
        l->setPosition(toScreen(r.pos) + CCPoint{0, 20.f + r.age * 40.f});
        l->setOpacity(static_cast<GLubyte>(255 * std::clamp(1.f - (r.age - 1.5f), 0.f, 1.f)));
        m_markerNode->addChild(l);
    }
    // shared stopwatch
    double secs = s.timerSeconds();
    if (s.m_timerRunning || secs > 0) {
        int t = static_cast<int>(secs);
        m_timer->setString(fmt::format("{:02}:{:02}:{:02}", t / 3600, (t / 60) % 60, t % 60).c_str());
        m_timer->setColor(s.m_timerRunning ? ccColor3B{120, 255, 120} : ccColor3B{255, 200, 80});
    } else {
        m_timer->setString("");
    }
}

void Overlay::updateToolLabels() {
    auto& s = Session::get();
    m_toolLabels->removeAllChildren();
    for (auto& t : s.m_tasks) {
        if (t.done) continue;
        auto l = label(t.text + (t.claim.empty() ? "" : " (" + t.claim + ")"), "chatFont.fnt", .5f, t.claim.empty() ? ccColor3B{255, 220, 80} : ccColor3B{120, 200, 255});
        l->setAnchorPoint({0, 0});
        l->setPosition(toScreen({t.rect[0], t.rect[3]}) + CCPoint{3, 2});
        m_toolLabels->addChild(l);
    }
    m_heatLegend->removeAllChildren();
    if (s.m_heatmap) {
        std::map<std::string, int> counts;
        for (auto& [id, uid] : s.m_authorOf) counts[uid]++;
        std::vector<std::pair<std::string, int>> v(counts.begin(), counts.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        float y = 0;
        auto title = label("HEATMAP", "goldFont.fnt", .4f);
        title->setAnchorPoint({1, 1});
        m_heatLegend->addChild(title);
        y -= 14;
        for (size_t i = 0; i < v.size() && i < 12; i++) {
            auto nm = s.m_names.count(v[i].first) ? s.m_names[v[i].first] : "Unknown";
            auto l = label(fmt::format("{} ({})", nm, v[i].second), "chatFont.fnt", .45f, s.colorForUid(v[i].first));
            l->setAnchorPoint({1, 1});
            l->setPosition({0, y});
            m_heatLegend->addChild(l);
            y -= 11;
        }
    }
}

void Overlay::spawnConfetti() {
    auto win = CCDirector::get()->getWinSize();
    ccColor3B colors[] = {{255, 80, 80}, {80, 255, 120}, {80, 160, 255}, {255, 220, 60}, {200, 90, 255}, {0, 230, 255}};
    for (int i = 0; i < 90; i++) {
        auto spr = CCSprite::createWithSpriteFrameName("square_01_001.png");
        if (!spr) continue;
        spr->setScale(.15f + (rand() % 10) / 60.f);
        spr->setColor(colors[rand() % 6]);
        spr->setPosition({static_cast<float>(rand() % static_cast<int>(win.width)), win.height + 10 + rand() % 60});
        spr->setRotation(static_cast<float>(rand() % 360));
        float dur = 1.8f + (rand() % 100) / 50.f;
        auto fall = CCMoveBy::create(dur, {static_cast<float>(rand() % 160 - 80), -(win.height + 80)});
        auto spin = CCRotateBy::create(dur, static_cast<float>(rand() % 720 - 360));
        spr->runAction(CCSequence::create(CCSpawn::create(fall, spin, nullptr), CCRemoveSelf::create(), nullptr));
        this->addChild(spr, 30);
    }
    auto big = label("LEVEL FINISHED!", "bigFont.fnt", .9f, theme().accent2);
    big->setPosition(win / 2);
    big->runAction(CCSequence::create(CCScaleTo::create(.3f, 1.1f), CCDelayTime::create(1.5f), CCFadeOut::create(.6f), CCRemoveSelf::create(), nullptr));
    this->addChild(big, 31);
}

// ---------------------------------------------------------------- credits
class CreditsPopup : public BasePopup {
public:
    static CreditsPopup* create() {
        auto r = new CreditsPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    bool initPopup() {
        if (!setup(330, 230, "Multiplayer Editor")) return false;
        auto c = m_size / 2;
        text("Made by x7wv", {c.width, m_size.height - 55}, .8f, "goldFont.fnt", theme().accent2);
        text("Build Geometry Dash levels live with your friends.", {c.width, m_size.height - 85}, .5f, "chatFont.fnt");
        text("Instant sync - live cursors - chat - roles - pings", {c.width, m_size.height - 105}, .45f, "chatFont.fnt", {200, 200, 255});
        text("playtest ghosts - follow cam - locks - host tools", {c.width, m_size.height - 120}, .45f, "chatFont.fnt", {200, 200, 255});
        text("timeline - tasks - votes - templates - heatmap - timelapse", {c.width, m_size.height - 135}, .45f, "chatFont.fnt", {200, 200, 255});
        text(fmt::format("Version {}", Mod::get()->getVersion().toVString()), {c.width, 50}, .4f, "chatFont.fnt", {170, 170, 170});
        text("Credits to x7wv", {c.width, 32}, .45f, "goldFont.fnt");
        return true;
    }
};
void openCreditsPopup() { if (auto p = CreditsPopup::create()) p->show(); }

// ---------------------------------------------------------------- host
class HostPopup : public BasePopup {
public:
    static HostPopup* create() {
        auto r = new HostPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    TextInput *m_name, *m_pw, *m_motd, *m_tags;
    bool m_public = true, m_approval = false;
    bool initPopup() {
        if (!setup(340, 270, "Host This Level")) return false;
        float cx = m_size.width / 2, top = m_size.height;
        auto lel = LevelEditorLayer::get();
        std::string def = lel && lel->m_level ? std::string(lel->m_level->m_levelName) : "My Collab";
        m_name = input("Room name", {cx, top - 60}, 260, def);
        m_pw = input("Password (optional)", {cx, top - 95}, 260);
        m_motd = input("Welcome message (optional)", {cx, top - 130}, 260);
        m_tags = input("Tags: megacollab, casual...", {cx, top - 165}, 260);
        toggle("Public", {cx - 120, top - 198}, true, [this](bool on) { m_public = on; });
        toggle("Approve joins", {cx + 10, top - 198}, false, [this](bool on) { m_approval = on; });
        button("Host!", {cx, 30}, [this](auto) {
            auto lel = LevelEditorLayer::get();
            if (!lel) return;
            HostOptions o;
            o.name = m_name->getString(); o.password = m_pw->getString();
            o.motd = m_motd->getString(); o.tags = m_tags->getString();
            o.isPublic = m_public; o.approval = m_approval;
            Session::get().host(lel, o);
            Notification::create("Creating room...", NotificationIcon::Loading, 2.f)->show();
            this->onClose(nullptr);
        });
        return true;
    }
};
void openHostPopup() { if (auto p = HostPopup::create()) p->show(); }

// ---------------------------------------------------------------- join
static void doJoin(std::string const& code, std::string const& pw) {
    auto lel = LevelEditorLayer::get();
    auto go = [code, pw](bool intoCurrent) {
        Session::get().m_joinIntoCurrent = intoCurrent;
        Session::get().join(code, pw);
        Notification::create("Joining " + code + "...", NotificationIcon::Loading, 2.f)->show();
    };
    if (lel && lel->m_objects->count() == 0) return go(true);
    if (lel) {
        createQuickPopup("Join Room",
            "Joining opens the room in a <cy>new level</c>.\nYou'll leave this editor, so <cr>save your level first</c> if you need to!",
            "Cancel", "Join", [go](auto, bool yes) { if (yes) go(false); });
        return;
    }
    go(false);
}

class JoinPopup : public BasePopup {
public:
    static JoinPopup* create(std::string const& code, bool pw) {
        auto r = new JoinPopup();
        if (r->init(code, pw)) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    TextInput *m_code, *m_pw;
    bool init(std::string const& code, bool needsPw) {
        if (!setup(300, 190, "Join a Room")) return false;
        float cx = m_size.width / 2, top = m_size.height;
        m_code = input("Room code", {cx, top - 65}, 200, code);
        m_code->setCommonFilter(CommonFilter::Alphanumeric);
        m_pw = input(needsPw ? "Password (required)" : "Password (if any)", {cx, top - 105}, 200);
        button("Join", {cx, 30}, [this](auto) {
            std::string code = m_code->getString();
            if (code.empty()) return;
            doJoin(code, m_pw->getString());
            this->onClose(nullptr);
        });
        return true;
    }
};
void openJoinPopup(std::string const& code, bool pw) { if (auto p = JoinPopup::create(code, pw)) p->show(); }

// ---------------------------------------------------------------- room browser
class BrowserPopup : public BasePopup {
public:
    static BrowserPopup* create() {
        auto r = new BrowserPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
    ~BrowserPopup() { Session::get().onRoomsUpdated = nullptr; }
protected:
    CCNode* m_list = nullptr;
    CCMenu* m_listMenu = nullptr;
    int m_page = 0;
    bool initPopup() {
        if (!setup(400, 280, "Room Browser")) return false;
        m_list = CCNode::create();
        m_mainLayer->addChild(m_list, 5);
        m_listMenu = CCMenu::create();
        m_listMenu->setPosition({0, 0});
        m_mainLayer->addChild(m_listMenu, 6);
        button("<", {25, m_size.height / 2}, [this](auto) { if (m_page > 0) { m_page--; rebuild(); } }, .6f, "GJ_button_02.png");
        button(">", {m_size.width - 25, m_size.height / 2}, [this](auto) { m_page++; rebuild(); }, .6f, "GJ_button_02.png");
        button("Refresh", {m_size.width / 2 - 60, 22}, [](auto) { Session::get().requestRooms(); }, .55f);
        button("Join by Code", {m_size.width / 2 + 60, 22}, [this](auto) { openJoinPopup(); this->onClose(nullptr); }, .55f);
        Session::get().onRoomsUpdated = [this] { rebuild(); };
        Session::get().requestRooms();
        text("Loading...", {m_size.width / 2, m_size.height / 2}, .5f)->setTag(99);
        return true;
    }
    void rebuild() {
        if (auto l = m_mainLayer->getChildByTag(99)) l->removeFromParent();
        m_list->removeAllChildren();
        m_listMenu->removeAllChildren();
        auto& rooms = Session::get().m_roomList;
        constexpr int PER = 5;
        int pages = std::max<int>(1, (static_cast<int>(rooms.size()) + PER - 1) / PER);
        m_page = std::min(m_page, pages - 1);
        if (rooms.empty()) {
            auto l = label("No public rooms right now. Host one!", "bigFont.fnt", .4f);
            l->setPosition(m_size / 2);
            m_list->addChild(l);
            return;
        }
        float y = m_size.height - 62;
        for (int i = m_page * PER; i < std::min<int>(rooms.size(), (m_page + 1) * PER); i++) {
            auto const& r = rooms[i];
            auto name = label(r.name + (r.locked ? "  [locked]" : "") + (r.finished ? "  [finished]" : ""), "bigFont.fnt", .38f, theme().accent2);
            name->setAnchorPoint({0, .5f});
            name->setPosition({50, y + 7});
            name->limitLabelWidth(230, .38f, .1f);
            m_list->addChild(name);
            std::string tags;
            for (auto& t : r.tags) tags += "#" + t + " ";
            auto info = label(fmt::format("host {}  |  {} online  |  {} objects  {}", r.host, r.players, r.objects, tags), "chatFont.fnt", .45f, {200, 200, 200});
            info->setAnchorPoint({0, .5f});
            info->setPosition({50, y - 8});
            info->limitLabelWidth(240, .45f, .1f);
            m_list->addChild(info);
            auto b = textButton("Join", [r, this](auto) {
                if (r.locked) openJoinPopup(r.code, true);
                else doJoin(r.code, "");
                this->onClose(nullptr);
            }, .55f);
            b->setPosition({m_size.width - 75, y});
            m_listMenu->addChild(b);
            y -= 38;
        }
        auto pg = label(fmt::format("Page {}/{}", m_page + 1, pages), "chatFont.fnt", .45f);
        pg->setPosition({m_size.width / 2, 45});
        m_list->addChild(pg);
    }
};
void openBrowserPopup() { if (auto p = BrowserPopup::create()) p->show(); }

// ---------------------------------------------------------------- players
class PlayersPopup : public BasePopup {
public:
    static PlayersPopup* create() {
        auto r = new PlayersPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
    ~PlayersPopup() { Session::get().onStateChanged = nullptr; }
protected:
    CCNode* m_list = nullptr;
    CCMenu* m_listMenu = nullptr;
    int m_page = 0;
    bool initPopup() {
        if (!setup(440, 290, "Players")) return false;
        m_list = CCNode::create();
        m_mainLayer->addChild(m_list, 5);
        m_listMenu = CCMenu::create();
        m_listMenu->setPosition({0, 0});
        m_mainLayer->addChild(m_listMenu, 6);
        button("<", {22, m_size.height / 2}, [this](auto) { if (m_page > 0) { m_page--; rebuild(); } }, .55f, "GJ_button_02.png");
        button(">", {m_size.width - 22, m_size.height / 2}, [this](auto) { m_page++; rebuild(); }, .55f, "GJ_button_02.png");
        button("Nickname", {m_size.width / 2, 22}, [](auto) {
            // reuse the chat-style text popup for the nickname
            auto p = createQuickPopup("Nickname", "Set your nickname in this room's chat by typing <cy>/nick yourname</c> in chat.", "OK", nullptr, [](auto, bool) {});
            (void)p;
        }, .5f);
        Session::get().onStateChanged = [this] { rebuild(); };
        rebuild();
        return true;
    }
    void rebuild() {
        m_list->removeAllChildren();
        m_listMenu->removeAllChildren();
        auto& s = Session::get();
        bool isHost = s.m_myRole == "host";
        float y = m_size.height - 55;

        // join requests first
        for (auto& [pid, name] : s.m_joinRequests) {
            auto l = label(name + " wants to join", "bigFont.fnt", .35f, {255, 220, 80});
            l->setAnchorPoint({0, .5f});
            l->setPosition({40, y});
            m_list->addChild(l);
            if (isHost) {
                int p = pid;
                auto a = textButton("Accept", [p](auto) { Session::get().approve(p, true); }, .45f);
                a->setPosition({m_size.width - 130, y});
                m_listMenu->addChild(a);
                auto d = textButton("Deny", [p](auto) { Session::get().approve(p, false); }, .45f, "GJ_button_06.png");
                d->setPosition({m_size.width - 65, y});
                m_listMenu->addChild(d);
            }
            y -= 28;
        }

        std::vector<Player const*> list;
        for (auto& [_, p] : s.m_players) list.push_back(&p);
        std::sort(list.begin(), list.end(), [](auto a, auto b) { return a->pid < b->pid; });
        constexpr int PER = 6;
        int pages = std::max<int>(1, (static_cast<int>(list.size()) + PER - 1) / PER);
        m_page = std::min(m_page, pages - 1);
        for (int i = m_page * PER; i < std::min<int>(list.size(), (m_page + 1) * PER); i++) {
            auto p = list[i];
            bool me = p->pid == s.m_myPid;
            auto gm = GameManager::get();
            auto icon = SimplePlayer::create(p->iconId > 0 ? p->iconId : 1);
            icon->setColors(gm->colorForIdx(p->c1), gm->colorForIdx(p->c2));
            if (p->glow) icon->setGlowOutline(gm->colorForIdx(p->c2));
            icon->setScale(.55f);
            icon->setPosition({48, y});
            m_list->addChild(icon);

            auto name = label(p->display() + (me ? " (you)" : ""), "bigFont.fnt", .38f, p->color);
            name->setAnchorPoint({0, .5f});
            name->setPosition({68, y + 6});
            name->limitLabelWidth(130, .38f, .1f);
            m_list->addChild(name);
            auto info = label(fmt::format("{}  |  {}ms  |  {} placed{}{}", p->role, p->ping, p->placed, p->afk ? "  |  afk" : "", p->playing ? "  |  playtesting" : ""),
                "chatFont.fnt", .4f, roleColor(p->role));
            info->setAnchorPoint({0, .5f});
            info->setPosition({68, y - 8});
            m_list->addChild(info);

            float bx = m_size.width - 40;
            auto addBtn = [&](char const* t, geode::Function<void(CCMenuItemSpriteExtra*)> cb, char const* bg = "GJ_button_04.png") {
                auto b = textButton(t, std::move(cb), .38f, bg);
                bx -= b->getScaledContentSize().width / 2;
                b->setPosition({bx, y});
                bx -= b->getScaledContentSize().width / 2 + 3;
                m_listMenu->addChild(b);
            };
            int pid = p->pid;
            if (!me) {
                if (isHost && p->role != "host") {
                    addBtn("Ban", [pid](auto) { Session::get().kick(pid, true); }, "GJ_button_06.png");
                    addBtn("Kick", [pid](auto) { Session::get().kick(pid, false); }, "GJ_button_06.png");
                    addBtn("Host", [pid](auto) {
                        createQuickPopup("Transfer Host", "Make this player the host? You'll become a builder.", "Cancel", "Yes",
                            [pid](auto, bool y) { if (y) Session::get().transferHost(pid); });
                    });
                    std::string next = p->role == "builder" ? "decorator" : p->role == "decorator" ? "viewer" : "builder";
                    addBtn("Role", [pid, next](auto) { Session::get().setRole(pid, next); });
                    addBtn("Undo", [pid](auto) {
                        createQuickPopup("Undo Player", "Undo this player's last <cy>10</c> edits?", "Cancel", "Undo",
                            [pid](auto, bool y) { if (y) Session::get().undoUser(pid, 10); });
                    }, "GJ_button_02.png");
                }
                addBtn("Jump", [pid](auto) { Session::get().jumpToPlayer(pid); });
                addBtn("Follow", [pid, this](auto) {
                    Session::get().followPlayer(pid);
                    Notification::create("Following - press Unfollow to stop", NotificationIcon::Info)->show();
                    this->onClose(nullptr);
                });
            }
            y -= 34;
        }
        auto pg = label(fmt::format("{} online  |  page {}/{}", list.size(), m_page + 1, pages), "chatFont.fnt", .45f);
        pg->setPosition({m_size.width / 2, 44});
        m_list->addChild(pg);
    }
};
void openPlayersPopup() { if (auto p = PlayersPopup::create()) p->show(); }

// ---------------------------------------------------------------- chat
class ChatPopup : public BasePopup {
public:
    static ChatPopup* create() {
        auto r = new ChatPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    TextInput* m_in = nullptr;
    CCNode* m_lines = nullptr;
    bool m_typing = false;
    size_t m_seen = 0;
    float m_typingTimer = 0;
    bool initPopup() {
        if (!setup(420, 270, "Chat")) return false;
        m_lines = CCNode::create();
        m_mainLayer->addChild(m_lines, 5);
        m_in = input("Say something... (/nick name)", {m_size.width / 2 - 40, 30}, 300);
        m_in->setMaxCharCount(300);
        m_in->setCallback([this](std::string const& s) {
            if (!m_typing && !s.empty()) { m_typing = true; Session::get().sendTyping(true); }
            m_typingTimer = 0;
        });
        button("Send", {m_size.width - 45, 30}, [this](auto) { send(); }, .6f);
        this->schedule(schedule_selector(ChatPopup::tick), .2f);
        rebuild();
        return true;
    }
    void send() {
        std::string t = m_in->getString();
        if (t.empty()) return;
        if (t.rfind("/nick ", 0) == 0) Session::get().setNick(t.substr(6));
        else Session::get().sendChat(t);
        m_in->setString("");
        if (m_typing) { m_typing = false; Session::get().sendTyping(false); }
    }
    void keyDown(enumKeyCodes key, double t) override {
        if (key == enumKeyCodes::KEY_Enter) { send(); return; }
        BasePopup::keyDown(key, t);
    }
    void tick(float dt) {
        m_typingTimer += dt;
        if (m_typing && m_typingTimer > 3.f) { m_typing = false; Session::get().sendTyping(false); }
        if (Session::get().m_chat.size() != m_seen || !Session::get().m_chat.empty()) rebuild();
    }
    void rebuild() {
        auto& chat = Session::get().m_chat;
        m_seen = chat.size();
        m_lines->removeAllChildren();
        float y = 58;
        int n = 0;
        for (auto it = chat.rbegin(); it != chat.rend() && n < 14; ++it, ++n) {
            std::string text = it->system ? it->text : it->name + ": " + it->text;
            auto l = label(text, "chatFont.fnt", .55f, it->system ? it->color : ccColor3B{255, 255, 255});
            l->setAnchorPoint({0, 0});
            l->setPosition({20, y});
            l->limitLabelWidth(m_size.width - 40, .55f, .2f);
            m_lines->addChild(l);
            y += 12.5f;
        }
    }
    void onClose(CCObject* s) override {
        if (m_typing) Session::get().sendTyping(false);
        BasePopup::onClose(s);
    }
};
void openChatPopup() { if (auto p = ChatPopup::create()) p->show(); }

// ---------------------------------------------------------------- room settings (host)
class RoomSettingsPopup : public BasePopup {
public:
    static RoomSettingsPopup* create() {
        auto r = new RoomSettingsPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    TextInput *m_motd, *m_pw;
    bool initPopup() {
        if (!setup(400, 290, "Room Settings")) return false;
        auto& s = Session::get();
        float cx = m_size.width / 2, top = m_size.height;
        toggle("Freeze edits", {40, top - 55}, s.m_frozen, [](bool on) { Session::get().setRoomOption(matjson::makeObject({{"frozen", on}})); });
        toggle("Public", {40, top - 80}, s.m_roomPublic, [](bool on) { Session::get().setRoomOption(matjson::makeObject({{"public", on}})); });
        toggle("Approve joins", {40, top - 105}, s.m_roomApproval, [](bool on) { Session::get().setRoomOption(matjson::makeObject({{"approval", on}})); });
        toggle("Level finished!", {40, top - 130}, s.m_finished, [](bool on) { Session::get().setRoomOption(matjson::makeObject({{"finished", on}})); });

        auto slowLbl = text(fmt::format("Chat slow mode: {}s", s.m_slowMs / 1000), {cx + 90, top - 55}, .35f);
        button("Change", {cx + 90, top - 75}, [slowLbl](auto) {
            static int const opts[] = {0, 3000, 10000, 30000};
            int cur = Session::get().m_slowMs, next = 0;
            for (size_t i = 0; i < 4; i++) if (opts[i] == cur) next = opts[(i + 1) % 4];
            Session::get().m_slowMs = next;
            slowLbl->setString(fmt::format("Chat slow mode: {}s", next / 1000).c_str());
            Session::get().setRoomOption(matjson::makeObject({{"slowMs", next}}));
        }, .45f);
        auto rateLbl = text(fmt::format("Edit limit: {}/s per player", s.m_rate), {cx + 90, top - 100}, .35f);
        button("Change", {cx + 90, top - 120}, [rateLbl](auto) {
            static int const opts[] = {50, 200, 400, 1000, 5000};
            int cur = Session::get().m_rate, next = 400;
            for (size_t i = 0; i < 5; i++) if (opts[i] == cur) next = opts[(i + 1) % 5];
            Session::get().m_rate = next;
            rateLbl->setString(fmt::format("Edit limit: {}/s per player", next).c_str());
            Session::get().setRoomOption(matjson::makeObject({{"rate", next}}));
        }, .45f);

        m_motd = input("Welcome message", {cx - 50, top - 170}, 260, s.m_motd);
        button("Set", {cx + 130, top - 170}, [this](auto) {
            Session::get().setRoomOption(matjson::makeObject({{"motd", std::string(m_motd->getString())}}));
            Notification::create("Welcome message updated", NotificationIcon::Success)->show();
        }, .5f);
        m_pw = input("New password (empty = none)", {cx - 50, top - 205}, 260);
        button("Set", {cx + 130, top - 205}, [this](auto) {
            Session::get().setRoomOption(matjson::makeObject({{"password", std::string(m_pw->getString())}}));
            Notification::create("Password updated", NotificationIcon::Success)->show();
        }, .5f);
        button("Lock Selected", {cx - 70, 28}, [](auto) { Session::get().lockSelection(true); }, .5f);
        button("Unlock Selected", {cx + 70, 28}, [](auto) { Session::get().lockSelection(false); }, .5f);
        return true;
    }
};
void openRoomSettingsPopup() { if (auto p = RoomSettingsPopup::create()) p->show(); }

// ---------------------------------------------------------------- main
class MainPopup : public BasePopup {
public:
    static MainPopup* create() {
        auto r = new MainPopup();
        if (r->initPopup()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
protected:
    bool initPopup() {
        auto& s = Session::get();
        if (!setup(340, 250, "Multiplayer Editor")) return false;
        float cx = m_size.width / 2, top = m_size.height;
        bool inEditor = LevelEditorLayer::get() != nullptr;
        text("by x7wv", {cx, top - 42}, .4f, "goldFont.fnt", theme().accent2);

        if (s.inRoom()) {
            text(s.m_roomName.empty() ? "Collab Room" : s.m_roomName, {cx, top - 65}, .5f);
            text(fmt::format("Code: {}  |  {} online  |  you are {}", s.m_code, s.m_players.size(), s.m_myRole), {cx, top - 85}, .5f, "chatFont.fnt", {200, 200, 255});
            button("Copy Invite", {cx - 80, top - 120}, [](auto) {
                auto& s = Session::get();
                geode::utils::clipboard::write(fmt::format("Join my GD collab! Multiplayer Editor (by x7wv) room code: {}  server: {}", s.m_code, Net::get().url()));
                Notification::create("Invite copied! Paste it in Discord", NotificationIcon::Success)->show();
            }, .6f);
            button("Players", {cx + 80, top - 120}, [](auto) { openPlayersPopup(); }, .6f);
            button("Chat", {cx - 80, top - 158}, [](auto) { openChatPopup(); }, .6f);
            if (s.m_myRole == "host") button("Room Settings", {cx + 80, top - 158}, [](auto) { openRoomSettingsPopup(); }, .6f);
            else button("Lock Selected", {cx + 80, top - 158}, [](auto) { Session::get().lockSelection(true); }, .6f);
            button("Leave Room", {cx - 80, 30}, [this](auto) {
                Session::get().leave();
                Notification::create("Left the room", NotificationIcon::Info)->show();
                this->onClose(nullptr);
            }, .6f, "GJ_button_06.png");
            button("Credits", {cx + 80, 30}, [](auto) { openCreditsPopup(); }, .6f, "GJ_button_02.png");
        } else {
            text(fmt::format("Server: {}", Mod::get()->getSettingValue<std::string>("server-url")), {cx, top - 65}, .45f, "chatFont.fnt", {180, 180, 180});
            if (inEditor) button("Host This Level", {cx, top - 105}, [this](auto) { openHostPopup(); this->onClose(nullptr); }, .7f);
            else text("Open a level in the editor to host it", {cx, top - 105}, .4f, "chatFont.fnt", {200, 200, 200});
            button("Join by Code", {cx - 80, top - 150}, [this](auto) { openJoinPopup(); this->onClose(nullptr); }, .6f);
            button("Browse Rooms", {cx + 80, top - 150}, [this](auto) { openBrowserPopup(); this->onClose(nullptr); }, .6f);
            button("Server Settings", {cx - 80, 30}, [](auto) { geode::openSettingsPopup(Mod::get()); }, .55f, "GJ_button_04.png");
            button("Credits", {cx + 80, 30}, [](auto) { openCreditsPopup(); }, .55f, "GJ_button_02.png");
        }
        return true;
    }
};
void openMainPopup() { if (auto p = MainPopup::create()) p->show(); }

}
