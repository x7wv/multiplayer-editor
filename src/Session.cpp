// Multiplayer Editor — made by x7wv
//
// Sync engine. Every object in the editor gets a network id. Local changes are
// found three ways, so nothing slips through:
//   1. hooks on create/paste/delete (instant)
//   2. selected objects are re-checked every 100ms (moves, rotates, popups)
//   3. a background sweeper walks the whole level a slice per frame (undo, odd cases)
// Changes are batched every 50ms and sent to the server, which orders them and
// relays them to everyone else.
#include "Session.hpp"
#include "Net.hpp"
#include "Json.hpp"
#include <Geode/utils/cocos.hpp>
#include <chrono>
#include <random>

using namespace geode::prelude;

namespace me {

// ---------------------------------------------------------------- lifecycle
Session& Session::get() {
    static Session inst;
    return inst;
}

Session::Session() {
    m_uid = Mod::get()->getSavedValue<std::string>("uid", "");
    if (m_uid.empty()) {
        m_uid = "u" + randomStr(15);
        Mod::get()->setSavedValue<std::string>("uid", m_uid);
    }
    m_prefix = randomStr(5);
    auto& net = Net::get();
    net.onMessage = [this](matjson::Value const& m) { this->handle(m); };
    net.onOpen = [this] { this->onOpen(); };
    net.onClose = [this](std::string const& r) { this->onClosed(r); };
}

bool Session::online() const { return Net::get().state() == NetState::Online; }

void Session::connect() {
    auto url = Mod::get()->getSettingValue<std::string>("server-url");
    Net::get().connect(url);
}

void Session::sendJoin() {
    sendRaw(matjson::makeObject({{"t", "join"}, {"code", m_code}, {"password", m_password}}));
}

void Session::sendRaw(matjson::Value const& msg) { Net::get().send(msg); }

void Session::sendHello() {
    auto gm = GameManager::get();
    std::string name = GJAccountManager::get()->m_username;
    if (name.empty()) name = gm->m_playerName;
    if (name.empty()) name = "Player";
    auto icon = matjson::makeObject({
        {"id", gm->getPlayerFrame()}, {"c1", gm->getPlayerColor()},
        {"c2", gm->getPlayerColor2()}, {"glow", gm->getPlayerGlow()},
    });
    auto col = gm->colorForIdx(gm->getPlayerColor());
    auto nick = Mod::get()->getSavedValue<std::string>("nick", "");
    sendRaw(matjson::makeObject({
        {"t", "hello"}, {"uid", m_uid}, {"name", name}, {"nick", nick},
        {"color", colorJson(col)}, {"icon", icon}, {"ver", Mod::get()->getVersion().toNonVString()},
    }));
}

void Session::onOpen() {
    sendHello();
    if (m_pendingHost && m_lel) {
        doCreate();
    } else if (m_wantRoom && !m_code.empty()) {
        m_isRejoin = m_hasJoined;
        sendJoin();
    }
    if (m_wantList) {
        m_wantList = false;
        sendRaw(matjson::makeObject({{"t", "list"}}));
    }
}

void Session::doCreate() {
    if (!m_pendingHost || !m_lel) return;
    {
        auto opts = *m_pendingHost;
        m_pendingHost.reset();
        // build the create message from what's in the editor right now
        clearMaps();
        auto objects = matjson::Value::array();
        auto arr = m_lel->m_objects;
        for (size_t i = 0; i < arr->count(); i++) {
            auto obj = static_cast<GameObject*>(arr->objectAtIndex(i));
            auto id = newId();
            auto s = saveString(obj);
            mapObject(obj, id, s);
            m_authorOf[id] = m_uid;
            auto entry = matjson::Value::array();
            entry.push(id); entry.push(s); entry.push(kindOf(obj));
            objects.push(entry);
        }
        m_lastSettings = currentSettings();
        auto tags = matjson::Value::array();
        for (auto& t : utils::string::split(opts.tags, ",")) {
            auto tt = utils::string::trim(t);
            if (!tt.empty()) tags.push(tt);
        }
        m_password = opts.password;
        sendRaw(matjson::makeObject({
            {"t", "create"}, {"name", opts.name}, {"password", opts.password}, {"public", opts.isPublic},
            {"approval", opts.approval}, {"motd", opts.motd}, {"tags", tags},
            {"settings", m_lastSettings}, {"objects", objects},
        }));
    }
}

void Session::onClosed(std::string const& reason) {
    if (m_wantRoom) {
        Notification::create("Multiplayer Editor: connection lost, reconnecting...", NotificationIcon::Warning)->show();
        // keep building offline; anything unacknowledged is re-sent after reconnect
        std::vector<matjson::Value> resend;
        for (auto& [n, ops] : m_inflight) resend.insert(resend.end(), ops.begin(), ops.end());
        m_outOps.insert(m_outOps.begin(), resend.begin(), resend.end());
        m_inflight.clear();
    }
    if (onStateChanged) onStateChanged();
}

void Session::host(LevelEditorLayer* lel, HostOptions const& opts) {
    if (inRoom()) return;
    m_lel = lel;
    m_pendingHost = opts;
    m_creatingRoom = true;
    this->connect();
    if (online()) doCreate();
}

void Session::join(std::string const& code, std::string const& password) {
    if (inRoom()) this->leave();
    m_code = utils::string::toUpper(utils::string::trim(code));
    m_password = password;
    m_wantRoom = true;
    m_isRejoin = false;
    m_hasJoined = false;
    m_creatingRoom = false;
    this->connect();
    if (online()) sendJoin();
}

void Session::requestRooms() {
    m_wantList = true;
    this->connect();
    if (online()) {
        m_wantList = false;
        sendRaw(matjson::makeObject({{"t", "list"}}));
    }
}

void Session::leave() {
    if (online() && (inRoom() || m_pendingApproval)) sendRaw(matjson::makeObject({{"t", "leave"}}));
    m_code.clear();
    m_wantRoom = false;
    m_hasJoined = false;
    m_creatingRoom = false;
    m_pendingApproval = false;
    m_pendingHost.reset();
    m_pendingSnapshot.reset();
    m_players.clear();
    m_chat.clear();
    m_feed.clear();
    m_markers.clear();
    m_locks.clear();
    m_authorOf.clear();
    m_joinRequests.clear();
    m_outOps.clear();
    m_inflight.clear();
    m_unsentIds.clear();
    m_followPid = 0;
    m_frozen = m_finished = false;
    m_tasks.clear(); m_votes.clear(); m_history.clear(); m_templates.clear();
    m_leaderboard.clear(); m_drawings.clear(); m_reactions.clear(); m_replay.clear();
    m_groupClaims.clear(); m_lastGroups.clear(); m_currentStroke.clear();
    m_drawMode = m_heatmap = false;
    m_timerRunning = false; m_timerAcc = 0;
    clearMaps();
    if (onStateChanged) onStateChanged();
}

void Session::attachEditor(LevelEditorLayer* lel) {
    m_lel = lel;
    if (m_pendingSnapshot) {
        auto snap = std::move(*m_pendingSnapshot);
        m_pendingSnapshot.reset();
        loadSnapshot(snap);
    }
}

void Session::detachEditor(LevelEditorLayer* lel) {
    if (m_lel != lel) return;
    m_lel = nullptr;
    if (inRoom()) {
        flushOps();
        this->leave();
        Notification::create("Left the collab room", NotificationIcon::Info)->show();
    }
    clearMaps();
}

// ---------------------------------------------------------------- id maps
GameObject* Session::objectFor(std::string const& id) const {
    auto it = m_idToObj.find(id);
    return it == m_idToObj.end() ? nullptr : it->second;
}
std::string Session::idFor(GameObject* obj) const {
    auto it = m_objToId.find(obj);
    return it == m_objToId.end() ? "" : it->second.id;
}
std::string Session::newId() { return m_prefix + ":" + base36(++m_counter); }
std::string Session::saveString(GameObject* obj) { return std::string(obj->getSaveString(m_lel)); }
std::string Session::kindOf(GameObject* obj) {
    if (obj->m_isTrigger) return "t";
    if (obj->m_objectType == GameObjectType::Decoration) return "d";
    return "g";
}
void Session::mapObject(GameObject* obj, std::string const& id, std::string const& s) {
    m_objToId[obj] = {id, s};
    m_idToObj[id] = obj;
}
void Session::unmapObject(GameObject* obj) {
    auto it = m_objToId.find(obj);
    if (it == m_objToId.end()) return;
    m_idToObj.erase(it->second.id);
    m_objToId.erase(it);
    m_touched.erase(obj);
}
void Session::clearMaps() {
    m_objToId.clear();
    m_idToObj.clear();
    m_touched.clear();
    m_sweepIdx = 0;
}

// ---------------------------------------------------------------- local changes
void Session::onLocalTouched(GameObject* obj) {
    if (!active() || applyingRemote || !obj) return;
    m_touched.insert(obj);
}

void Session::onLocalRemoved(GameObject* obj) {
    if (!active() || !obj) return;
    auto it = m_objToId.find(obj);
    if (it == m_objToId.end()) return;
    if (!applyingRemote) queueOp("del", it->second.id, nullptr, kindOf(obj));
    unmapObject(obj);
}

void Session::checkObject(GameObject* obj) {
    auto it = m_objToId.find(obj);
    if (it == m_objToId.end()) {
        auto id = newId();
        auto s = saveString(obj);
        mapObject(obj, id, s);
        m_authorOf[id] = m_uid;
        queueOp("add", id, &s, kindOf(obj));
        return;
    }
    auto s = saveString(obj);
    if (s != it->second.s) {
        it->second.s = s;
        queueOp("upd", it->second.id, &s, kindOf(obj));
    }
}

void Session::queueOp(char const* o, std::string const& id, std::string const* s, std::string const& kind) {
    auto op = matjson::makeObject({{"o", o}, {"id", id}, {"k", kind}});
    if (s) op["s"] = *s;
    m_outOps.push_back(std::move(op));
    m_unsentIds.insert(id);
}

void Session::flushOps() {
    if (m_outOps.empty() || !online() || !inRoom()) return;
    // a few thousand ops per packet keeps big pastes snappy
    while (!m_outOps.empty()) {
        size_t n = std::min<size_t>(m_outOps.size(), 2000);
        auto ops = matjson::Value::array();
        std::vector<matjson::Value> copy(m_outOps.begin(), m_outOps.begin() + n);
        for (auto& op : copy) ops.push(op);
        m_outOps.erase(m_outOps.begin(), m_outOps.begin() + n);
        int batch = ++m_batchN;
        m_inflight[batch] = std::move(copy);
        sendRaw(matjson::makeObject({{"t", "ops"}, {"n", batch}, {"ops", ops}}));
    }
}

void Session::sweepStep(int budget) {
    auto arr = m_lel->m_objects;
    size_t n = arr->count();
    size_t end = std::min(n, m_sweepIdx + static_cast<size_t>(budget));
    for (size_t i = m_sweepIdx; i < end; i++) checkObject(static_cast<GameObject*>(arr->objectAtIndex(i)));
    m_sweepIdx = end;
    if (m_sweepIdx >= n) {
        // cycle finished: anything we track that's no longer in the level was deleted
        std::unordered_set<GameObject*> alive;
        alive.reserve(n * 2);
        for (size_t i = 0; i < n; i++) alive.insert(static_cast<GameObject*>(arr->objectAtIndex(i)));
        std::vector<GameObject*> gone;
        for (auto& [obj, rec] : m_objToId) if (!alive.count(obj)) gone.push_back(obj);
        for (auto obj : gone) {
            queueOp("del", m_objToId[obj].id, nullptr, "g");
            unmapObject(obj);
        }
        m_sweepIdx = 0;
        m_fastSweep = false;
    }
}

// ---------------------------------------------------------------- remote changes
GameObject* Session::createRemote(std::string const& s) {
    auto arr = m_lel->createObjectsFromString(gd::string(s + ";"), true, true);
    if (!arr || arr->count() == 0) return nullptr;
    return static_cast<GameObject*>(arr->objectAtIndex(0));
}

void Session::removeRemote(GameObject* obj) {
    if (obj->m_isSelected && m_lel->m_editorUI) m_lel->m_editorUI->deselectObject(obj);
    unmapObject(obj);
    m_lel->removeObject(obj, true);
}

void Session::applyOps(matjson::Value const& ops) {
    if (!m_lel || !ops.isArray()) return;
    applyingRemote = true;
    for (auto const& op : ops) {
        auto o = jstr(op, "o"), id = jstr(op, "id"), s = jstr(op, "s");
        if (id.empty()) continue;
        auto existing = objectFor(id);
        if (o == "del") {
            if (existing) removeRemote(existing);
            m_authorOf.erase(id);
            m_locks.erase(id);
            continue;
        }
        if (s.empty()) continue;
        bool wasSelected = false;
        if (existing) {
            // already identical? (our own echo after a resync)
            if (existing->m_isSelected) wasSelected = true;
            removeRemote(existing);
        }
        if (auto obj = createRemote(s)) {
            mapObject(obj, id, saveString(obj));
            if (wasSelected && m_lel->m_editorUI) m_lel->m_editorUI->selectObject(obj, true);
        }
    }
    applyingRemote = false;
}

void Session::applyFix(matjson::Value const& fix) {
    if (!fix.isArray()) return;
    auto ops = matjson::Value::array();
    for (auto const& f : fix) {
        auto id = jstr(f, "id");
        m_unsentIds.erase(id);
        bool hasS = f.contains("s") && f["s"].isString();
        if (hasS) ops.push(matjson::makeObject({{"o", "upd"}, {"id", id}, {"s", jstr(f, "s")}}));
        else ops.push(matjson::makeObject({{"o", "del"}, {"id", id}}));
    }
    applyOps(ops);
}

void Session::loadSnapshot(matjson::Value const& m) {
    if (!m_lel) { m_pendingSnapshot = m; return; }
    applyingRemote = true;
    auto ui = m_lel->m_editorUI;
    if (ui) ui->deselectAll();

    // index what the server has
    std::unordered_map<std::string, std::string> server;
    if (jarr(m, "objects")) {
        for (auto const& e : m["objects"]) {
            if (!e.isArray() || e.size() < 2) continue;
            auto id = e[0].asString(), s = e[1].asString();
            if (!id || !s) continue;
            server[id.unwrap()] = s.unwrap();
            if (e.size() > 2) if (auto a = e[2].asString()) m_authorOf[id.unwrap()] = a.unwrap();
        }
    }

    // 1) remove local objects the server doesn't know (unless we made them offline)
    std::vector<GameObject*> toRemove;
    auto arr = m_lel->m_objects;
    for (size_t i = 0; i < arr->count(); i++) {
        auto obj = static_cast<GameObject*>(arr->objectAtIndex(i));
        auto id = idFor(obj);
        if (id.empty() || (!server.count(id) && !m_unsentIds.count(id))) toRemove.push_back(obj);
    }
    for (auto obj : toRemove) { unmapObject(obj); m_lel->removeObject(obj, true); }

    // 2) create/update everything else, in batches for speed
    std::vector<std::pair<std::string, std::string>> toCreate;
    for (auto& [id, s] : server) {
        if (m_unsentIds.count(id)) continue; // our offline edit wins
        if (auto existing = objectFor(id)) {
            unmapObject(existing);
            m_lel->removeObject(existing, true);
        }
        toCreate.emplace_back(id, s);
    }
    constexpr size_t BATCH = 400;
    for (size_t i = 0; i < toCreate.size(); i += BATCH) {
        size_t end = std::min(toCreate.size(), i + BATCH);
        std::string joined;
        for (size_t j = i; j < end; j++) { joined += toCreate[j].second; joined += ';'; }
        auto made = m_lel->createObjectsFromString(gd::string(joined), true, true);
        if (made && made->count() == end - i) {
            for (size_t j = i; j < end; j++) {
                auto obj = static_cast<GameObject*>(made->objectAtIndex(j - i));
                mapObject(obj, toCreate[j].first, saveString(obj));
            }
        } else {
            // something didn't parse: undo the batch and go one by one so ids stay correct
            if (made) for (size_t k = 0; k < made->count(); k++) m_lel->removeObject(static_cast<GameObject*>(made->objectAtIndex(k)), true);
            for (size_t j = i; j < end; j++) {
                if (auto obj = createRemote(toCreate[j].second)) mapObject(obj, toCreate[j].first, saveString(obj));
            }
        }
    }
    applyingRemote = false;

    auto settings = jstr(m, "settings");
    if (!settings.empty()) applySettings(settings);
    m_sweepIdx = 0;
}

// ---------------------------------------------------------------- level settings
std::string Session::currentSettings() {
    if (!m_lel || !m_lel->m_levelSettings) return "";
    auto lvl = m_lel->m_level;
    return matjson::makeObject({
        {"ls", std::string(m_lel->m_levelSettings->getSaveString())},
        {"song", lvl ? lvl->m_songID : 0},
        {"track", lvl ? lvl->m_audioTrack : 0},
    }).dump(matjson::NO_INDENTATION);
}

void Session::applySettings(std::string const& s) {
    if (!m_lel || s.empty()) return;
    auto parsed = matjson::parse(s);
    if (!parsed) return;
    auto v = parsed.unwrap();
    auto ls = m_lel->m_levelSettings;
    auto ls2 = jstr(v, "ls");
    if (ls && !ls2.empty()) {
        if (auto o = LevelSettingsObject::objectFromString(gd::string(ls2))) {
            ls->m_startMode = o->m_startMode;
            ls->m_startSpeed = o->m_startSpeed;
            ls->m_startMini = o->m_startMini;
            ls->m_startDual = o->m_startDual;
            ls->m_mirrorMode = o->m_mirrorMode;
            ls->m_rotateGameplay = o->m_rotateGameplay;
            ls->m_twoPlayerMode = o->m_twoPlayerMode;
            ls->m_platformerMode = o->m_platformerMode;
            ls->m_songOffset = o->m_songOffset;
            ls->m_fadeIn = o->m_fadeIn;
            ls->m_fadeOut = o->m_fadeOut;
            ls->m_backgroundIndex = o->m_backgroundIndex;
            ls->m_groundIndex = o->m_groundIndex;
            ls->m_middleGroundIndex = o->m_middleGroundIndex;
            ls->m_fontIndex = o->m_fontIndex;
            ls->m_isFlipped = o->m_isFlipped;
            ls->m_reverseGameplay = o->m_reverseGameplay;
            ls->m_disableStartPos = o->m_disableStartPos;
            if (ls->m_guidelineString != o->m_guidelineString) {
                ls->m_guidelineString = o->m_guidelineString;
                ls->m_guidelinesUpdated = true;
            }
        }
    }
    if (!ls2.empty()) applyColors(ls2);
    if (auto lvl = m_lel->m_level) {
        lvl->m_songID = jint(v, "song", lvl->m_songID);
        lvl->m_audioTrack = jint(v, "track", lvl->m_audioTrack);
    }
    m_lel->levelSettingsUpdated();
    m_lastSettings = currentSettings();
}

// ---------------------------------------------------------------- presence
void Session::sendPresence() {
    auto layer = m_lel->m_objectLayer;
    if (!layer) return;
    CCPoint screen;
#ifdef GEODE_IS_DESKTOP
    screen = geode::cocos::getMousePos();
#else
    screen = lastTouch;
#endif
    auto world = layer->convertToNodeSpace(screen);
    auto win = CCDirector::get()->getWinSize();
    auto cam = layer->convertToNodeSpace(CCPoint{win.width / 2, win.height / 2});
    auto msg = matjson::makeObject({
        {"t", "cursor"}, {"x", world.x}, {"y", world.y}, {"cx", cam.x}, {"cy", cam.y}, {"z", layer->getScale()},
    });
    // selection (only when it changes)
    std::vector<std::string> sel;
    if (auto ui = m_lel->m_editorUI) {
        if (auto arr = ui->getSelectedObjects()) {
            for (size_t i = 0; i < arr->count() && i < 2000; i++) {
                auto id = idFor(static_cast<GameObject*>(arr->objectAtIndex(i)));
                if (!id.empty()) sel.push_back(id);
            }
        }
    }
    if (sel != m_lastSel) {
        auto a = matjson::Value::array();
        for (auto& s : sel) a.push(s);
        msg["sel"] = a;
        m_lastSel = std::move(sel);
    }
    sendRaw(msg);
}

void Session::sendPlaytest() {
    bool playing = m_lel->m_playbackMode == PlaybackMode::Playing && m_lel->m_player1;
    if (playing) {
        auto p = m_lel->m_player1;
        sendRaw(matjson::makeObject({
            {"t", "play"}, {"on", true}, {"x", p->getPositionX()}, {"y", p->getPositionY()},
            {"r", p->getRotation()}, {"d", p->m_isDead},
        }));
    } else if (m_wasPlaying) {
        sendRaw(matjson::makeObject({{"t", "play"}, {"on", false}}));
    }
    m_wasPlaying = playing;
}

void Session::jumpToPlayer(int pid) {
    auto it = m_players.find(pid);
    if (it == m_players.end() || !m_lel || !m_lel->m_objectLayer) return;
    auto layer = m_lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    layer->setPosition(CCPoint{win.width / 2, win.height / 2} - it->second.camera * layer->getScale());
}

// ---------------------------------------------------------------- tick
void Session::update(float dt) {
    // age chat/feed/markers even outside the editor
    for (auto& c : m_chat) c.age += dt;
    for (auto& f : m_feed) f.age += dt;
    for (auto& mk : m_markers) mk.age += dt;
    std::erase_if(m_markers, [](Marker const& mk) { return mk.age > 8.f; });
    for (auto& r : m_reactions) r.age += dt;
    std::erase_if(m_reactions, [](Reaction const& r) { return r.age > 2.5f; });
    while (m_feed.size() > 4 || (!m_feed.empty() && m_feed.front().age > 6.f)) m_feed.pop_front();

    if (!active()) return;

    // 1) objects touched by hooks
    if (!m_touched.empty()) {
        auto touched = std::move(m_touched);
        m_touched.clear();
        for (auto obj : touched) checkObject(obj);
    }
    // 2) selection check
    m_selTimer += dt;
    if (m_selTimer >= 0.1f) {
        m_selTimer = 0;
        if (auto ui = m_lel->m_editorUI) {
            if (auto arr = ui->getSelectedObjects()) {
                for (size_t i = 0; i < arr->count(); i++) checkObject(static_cast<GameObject*>(arr->objectAtIndex(i)));
            }
        }
    }
    // 3) sweeper
    sweepStep(m_fastSweep ? 6000 : 400);

    // level settings
    m_setTimer += dt;
    if (m_setTimer >= 1.f) {
        m_setTimer = 0;
        auto s = currentSettings();
        if (!s.empty() && s != m_lastSettings) {
            m_lastSettings = s;
            if (m_myRole == "builder" || m_myRole == "host") sendRaw(matjson::makeObject({{"t", "settings"}, {"s", s}}));
        }
    }

    // flush edits every 50ms
    m_flushTimer += dt;
    if (m_flushTimer >= 0.05f) { m_flushTimer = 0; flushOps(); }

    if (!online()) return;

    // presence
    m_presTimer += dt;
    float presInterval = 1.f / std::max<int64_t>(2, Mod::get()->getSettingValue<int64_t>("cursor-rate"));
    if (m_presTimer >= presInterval) { m_presTimer = 0; sendPresence(); }
    m_playTimer += dt;
    if (m_playTimer >= 1.f / 15.f) { m_playTimer = 0; sendPlaytest(); }

    // latency
    m_latTimer += dt;
    if (m_latTimer >= 2.f) {
        m_latTimer = 0;
        m_latSentAt = nowMs();
        sendRaw(matjson::makeObject({{"t", "lat"}, {"ts", m_latSentAt}, {"rtt", m_rtt}}));
    }

    // ghost camera
    if (m_followPid) {
        auto it = m_players.find(m_followPid);
        if (it == m_players.end()) m_followPid = 0;
        else if (auto layer = m_lel->m_objectLayer) {
            auto& p = it->second;
            if (std::abs(layer->getScale() - p.zoom) > 0.01f && m_lel->m_editorUI) m_lel->m_editorUI->updateZoom(p.zoom);
            auto win = CCDirector::get()->getWinSize();
            auto target = CCPoint{win.width / 2, win.height / 2} - p.camera * layer->getScale();
            auto cur = layer->getPosition();
            layer->setPosition(cur + (target - cur) * std::min(1.f, dt * 10.f));
        }
    }
}

// ---------------------------------------------------------------- actions
void Session::sendChat(std::string const& text) {
    if (text.empty()) return;
    sendRaw(matjson::makeObject({{"t", "chat"}, {"text", text}}));
}
void Session::sendTyping(bool on) { sendRaw(matjson::makeObject({{"t", "typing"}, {"on", on}})); }
void Session::sendPing(CCPoint world, std::string const& label) {
    sendRaw(matjson::makeObject({{"t", "ping"}, {"x", world.x}, {"y", world.y}, {"label", label}}));
}
void Session::setRole(int pid, std::string const& role) { sendRaw(matjson::makeObject({{"t", "role"}, {"pid", pid}, {"role", role}})); }
void Session::kick(int pid, bool ban) { sendRaw(matjson::makeObject({{"t", "kick"}, {"pid", pid}, {"ban", ban}})); }
void Session::transferHost(int pid) { sendRaw(matjson::makeObject({{"t", "transfer"}, {"pid", pid}})); }
void Session::approve(int pid, bool ok) {
    sendRaw(matjson::makeObject({{"t", "approve"}, {"pid", pid}, {"ok", ok}}));
    std::erase_if(m_joinRequests, [pid](auto const& r) { return r.first == pid; });
}
void Session::setRoomOption(matjson::Value opts) {
    opts["t"] = "room";
    sendRaw(opts);
}
void Session::setNick(std::string const& nick) {
    Mod::get()->setSavedValue<std::string>("nick", nick);
    sendRaw(matjson::makeObject({{"t", "nick"}, {"nick", nick}}));
}
void Session::lockSelection(bool on) {
    if (!m_lel || !m_lel->m_editorUI) return;
    auto ids = matjson::Value::array();
    if (auto arr = m_lel->m_editorUI->getSelectedObjects()) {
        for (size_t i = 0; i < arr->count(); i++) {
            auto id = idFor(static_cast<GameObject*>(arr->objectAtIndex(i)));
            if (!id.empty()) ids.push(id);
        }
    }
    if (ids.size() == 0) {
        Notification::create("Select some objects first", NotificationIcon::Info)->show();
        return;
    }
    sendRaw(matjson::makeObject({{"t", "lock"}, {"ids", ids}, {"on", on}}));
}

// ---------------------------------------------------------------- chat helpers
void Session::addChat(std::string name, std::string text, ccColor3B color, bool system) {
    m_chat.push_back({std::move(name), std::move(text), color, 0.f, system});
    while (m_chat.size() > 60) m_chat.pop_front();
}
void Session::addFeed(std::string text) {
    m_feed.push_back({"", std::move(text), {200, 200, 200}, 0.f, true});
}
void Session::sfx(char const* file) {
    if (Mod::get()->getSettingValue<bool>("sounds")) FMODAudioEngine::get()->playEffect(file);
}

void Session::readPlayer(Player& p, matjson::Value const& v) {
    if (v.contains("pid")) p.pid = jint(v, "pid");
    if (v.contains("uid")) p.uid = jstr(v, "uid");
    if (v.contains("name")) p.name = jstr(v, "name");
    if (v.contains("nick")) p.nick = jstr(v, "nick");
    if (v.contains("role")) p.role = jstr(v, "role");
    if (v.contains("color")) p.color = jcolor(v, "color");
    if (v.contains("afk")) p.afk = jbool(v, "afk");
    if (v.contains("playing")) p.playing = jbool(v, "playing");
    if (v.contains("ping")) p.ping = jint(v, "ping");
    if (v.contains("placed")) p.placed = jint(v, "placed");
    if (v.contains("icon") && v["icon"].isObject()) {
        auto const& ic = v["icon"];
        p.iconId = jint(ic, "id", 1); p.c1 = jint(ic, "c1"); p.c2 = jint(ic, "c2", 3); p.glow = jbool(ic, "glow");
    }
    if (!p.uid.empty()) m_names[p.uid] = p.display();
}

// ---------------------------------------------------------------- message handling
void Session::enterRoom(matjson::Value const& m, bool isReset) {
    m_code = jstr(m, "code", m_code);
    m_wantRoom = true;
    m_hasJoined = true;
    m_pendingApproval = false;
    if (!isReset) {
        m_myPid = jint(m, "you");
        m_myRole = jstr(m, "role", "builder");
    }
    if (m.contains("room")) {
        auto const& r = m["room"];
        m_roomName = jstr(r, "name"); m_motd = jstr(r, "motd");
        m_frozen = jbool(r, "frozen"); m_finished = jbool(r, "finished");
        m_slowMs = jint(r, "slowMs"); m_rate = jint(r, "rate", 400);
        m_roomPublic = jbool(r, "public", true); m_roomApproval = jbool(r, "approval");
    }
    m_players.clear();
    if (jarr(m, "players")) for (auto const& pv : m["players"]) {
        Player p; readPlayer(p, pv);
        m_players[p.pid] = p;
    }
    if (m.contains("names") && m["names"].isObject()) {
        for (auto const& [uid, name] : m["names"]) if (auto s = name.asString()) m_names[uid] = s.unwrap();
    }
    m_locks.clear();
    if (jarr(m, "locks")) for (auto const& l : m["locks"]) if (auto s = l.asString()) m_locks.insert(s.unwrap());
    if (!isReset) {
        m_chat.clear();
        if (jarr(m, "chat")) for (auto const& c : m["chat"]) addChat(jstr(c, "name"), jstr(c, "text"), jcolor(c, "color"));
        for (auto& c : m_chat) c.age = 100.f; // history doesn't pop up on screen
    }

    if (m_creatingRoom && !isReset) {
        // we just uploaded this level, it's already mapped
        m_creatingRoom = false;
        Notification::create("Room created! Code: " + m_code, NotificationIcon::Success, 4.f)->show();
        sfx("playSound_01.ogg");
    } else if (m_lel && (m_isRejoin || isReset)) {
        loadSnapshot(m);
        if (m_isRejoin) Notification::create("Reconnected!", NotificationIcon::Success)->show();
    } else if (m_lel && m_joinIntoCurrent) {
        loadSnapshot(m);
    } else {
        // open a fresh local level for this collab, then load the room into it
        m_pendingSnapshot = m;
        auto level = GameLevelManager::sharedState()->createNewLevel();
        level->m_levelName = gd::string(("MP " + (m_roomName.empty() ? m_code : m_roomName)).substr(0, 20));
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelEditorLayer::scene(level, false)));
        sfx("playSound_01.ogg");
    }
    m_joinIntoCurrent = false;
    m_isRejoin = false;
    readExtras(m);
    if (!isReset && !m_motd.empty()) addChat("MOTD", m_motd, {255, 220, 80}, true);
    if (onStateChanged) onStateChanged();
}

void Session::handle(matjson::Value const& m) {
    auto t = jstr(m, "t");
    if (t == "welcome") {
        // nothing; the server is happy
    } else if (t == "rooms") {
        m_roomList.clear();
        if (jarr(m, "list")) for (auto const& r : m["list"]) {
            RoomInfo info;
            info.code = jstr(r, "code"); info.name = jstr(r, "name"); info.host = jstr(r, "host");
            info.players = jint(r, "players"); info.objects = jint(r, "objects");
            info.locked = jbool(r, "locked"); info.approval = jbool(r, "approval"); info.finished = jbool(r, "finished");
            if (jarr(r, "tags")) for (auto const& tg : r["tags"]) if (auto s = tg.asString()) info.tags.push_back(s.unwrap());
            m_roomList.push_back(info);
        }
        if (onRoomsUpdated) onRoomsUpdated();
    } else if (t == "joined") {
        enterRoom(m, false);
    } else if (t == "reset") {
        enterRoom(m, true);
    } else if (t == "pending") {
        m_pendingApproval = true;
        m_code = jstr(m, "code", m_code);
        Notification::create("Waiting for the host to let you in...", NotificationIcon::Loading, 5.f)->show();
    } else if (t == "ops") {
        applyOps(m["ops"]);
        int by = jint(m, "by");
        if (jarr(m, "ops")) {
            auto it = m_players.find(by);
            auto uid = it != m_players.end() ? it->second.uid : std::string("timeline");
            for (auto const& op : m["ops"]) {
                auto id = jstr(op, "id");
                if (jstr(op, "o") == "add") {
                    m_authorOf[id] = uid;
                    if (it != m_players.end()) it->second.placed++;
                }
            }
        }
    } else if (t == "ack") {
        int n = jint(m, "n");
        auto it = m_inflight.find(n);
        if (it != m_inflight.end()) {
            for (auto& op : it->second) m_unsentIds.erase(jstr(op, "id"));
            m_inflight.erase(it);
        }
    } else if (t == "reject") {
        applyFix(m["fix"]);
        auto reason = jstr(m, "reason", "Edit rejected");
        if (nowMs() - m_lastRejectNote > 3000) {
            m_lastRejectNote = nowMs();
            Notification::create(reason, NotificationIcon::Warning)->show();
        }
    } else if (t == "settings") {
        applySettings(jstr(m, "s"));
    } else if (t == "cursor") {
        int pid = jint(m, "p");
        auto it = m_players.find(pid);
        if (it == m_players.end()) return;
        auto& p = it->second;
        if (jbool(m, "gone")) { p.hasCursor = false; p.selection.clear(); return; }
        p.cursor = CCPoint(static_cast<float>(jnum(m, "x")), static_cast<float>(jnum(m, "y")));
        p.camera = CCPoint(static_cast<float>(jnum(m, "cx")), static_cast<float>(jnum(m, "cy")));
        p.zoom = static_cast<float>(jnum(m, "z", 1));
        p.hasCursor = true;
        if (jarr(m, "sel")) {
            p.selection.clear();
            for (auto const& s : m["sel"]) if (auto str = s.asString()) p.selection.push_back(str.unwrap());
        }
    } else if (t == "play") {
        auto it = m_players.find(jint(m, "p"));
        if (it == m_players.end()) return;
        it->second.playOn = jbool(m, "on");
        it->second.playPos = CCPoint(static_cast<float>(jnum(m, "x")), static_cast<float>(jnum(m, "y")));
        it->second.playRot = static_cast<float>(jnum(m, "r"));
    } else if (t == "chat") {
        addChat(jstr(m, "name"), jstr(m, "text"), jcolor(m, "color"));
        auto it = m_players.find(jint(m, "p"));
        if (it != m_players.end()) it->second.typing = false;
        if (jint(m, "p") != m_myPid) sfx("counter003.ogg");
    } else if (t == "typing") {
        auto it = m_players.find(jint(m, "p"));
        if (it != m_players.end()) it->second.typing = jbool(m, "on");
    } else if (t == "ping") {
        m_markers.push_back({{static_cast<float>(jnum(m, "x")), static_cast<float>(jnum(m, "y"))},
            jstr(m, "label"), jstr(m, "name"), jcolor(m, "color"), 0.f});
        sfx("counter003.ogg");
    } else if (t == "player+") {
        Player p; readPlayer(p, m["p"]);
        m_players[p.pid] = p;
        addChat("", p.display() + " joined", {120, 255, 120}, true);
        sfx("playSound_01.ogg");
    } else if (t == "player-") {
        int pid = jint(m, "pid");
        auto it = m_players.find(pid);
        if (it != m_players.end()) {
            addChat("", it->second.display() + " " + jstr(m, "reason", "left"), {255, 140, 120}, true);
            m_players.erase(it);
            sfx("quitSound_01.ogg");
        }
        if (m_followPid == pid) m_followPid = 0;
    } else if (t == "player~") {
        auto const& pv = m["p"];
        int pid = jint(pv, "pid");
        auto it = m_players.find(pid);
        if (it != m_players.end()) readPlayer(it->second, pv);
        if (pid == m_myPid && pv.contains("role")) {
            m_myRole = jstr(pv, "role");
            Notification::create("Your role is now " + m_myRole, NotificationIcon::Info)->show();
        }
    } else if (t == "room~") {
        auto const& r = m["room"];
        bool wasFrozen = m_frozen;
        m_roomName = jstr(r, "name", m_roomName); m_motd = jstr(r, "motd", m_motd);
        m_frozen = jbool(r, "frozen"); m_finished = jbool(r, "finished");
        m_slowMs = jint(r, "slowMs"); m_rate = jint(r, "rate", 400);
        m_roomPublic = jbool(r, "public", true); m_roomApproval = jbool(r, "approval");
        if (m_frozen != wasFrozen) addChat("", m_frozen ? "The host froze the room" : "The room is unfrozen", {120, 200, 255}, true);
    } else if (t == "locks") {
        bool on = jbool(m, "on");
        if (jarr(m, "ids")) for (auto const& i : m["ids"]) if (auto s = i.asString()) {
            if (on) m_locks.insert(s.unwrap()); else m_locks.erase(s.unwrap());
        }
    } else if (t == "feed") {
        auto uid = jstr(m, "uid");
        if (uid != m_uid) addFeed(jstr(m, "name") + " " + jstr(m, "text"));
    } else if (t == "achievement") {
        addChat("", jstr(m, "name") + " unlocked: " + jstr(m, "title"), {255, 215, 0}, true);
        if (jint(m, "p") == m_myPid) {
            Notification::create("Achievement: " + jstr(m, "title"), NotificationIcon::Success, 3.f)->show();
            sfx("achievement_01.ogg");
        }
    } else if (t == "system") {
        addChat("", jstr(m, "text"), {120, 200, 255}, true);
    } else if (t == "confetti") {
        m_confettiPending += 1;
        addChat("", jstr(m, "by") + " marked the level as FINISHED!", {255, 215, 0}, true);
        sfx("achievement_01.ogg");
    } else if (t == "kicked") {
        auto reason = jstr(m, "reason", "Removed from room");
        FLAlertLayer::create("Multiplayer Editor", gd::string(reason), "OK")->show();
        m_wantRoom = false;
        this->leave();
    } else if (t == "error") {
        Notification::create(jstr(m, "msg", "Error"), NotificationIcon::Error)->show();
        if (m_creatingRoom) { m_creatingRoom = false; }
    } else if (t == "joinRequest") {
        m_joinRequests.emplace_back(jint(m, "pid"), jstr(m, "name"));
        addChat("", jstr(m, "name") + " wants to join (open Players to approve)", {255, 220, 80}, true);
        sfx("counter003.ogg");
    } else if (t == "lat") {
        m_rtt = static_cast<int>(nowMs() - jnum(m, "ts"));
    }
    handleExtra(t, m);
    if (onStateChanged && (t == "player+" || t == "player-" || t == "player~" || t == "room~" || t == "joinRequest")) onStateChanged();
}

}
