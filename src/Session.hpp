// Multiplayer Editor — made by x7wv
#pragma once
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace me {

struct Player {
    int pid = 0;
    std::string uid, name, nick, role = "builder";
    cocos2d::ccColor3B color = {0, 200, 255};
    int iconId = 1, c1 = 0, c2 = 3;
    bool glow = false, afk = false, playing = false, typing = false;
    int ping = 0, placed = 0;
    // presence
    cocos2d::CCPoint cursor = {0, 0}, camera = {0, 0};
    float zoom = 1.f;
    bool hasCursor = false;
    std::vector<std::string> selection;
    // playtest ghost
    cocos2d::CCPoint playPos = {0, 0};
    float playRot = 0.f;
    bool playOn = false;

    std::string display() const { return nick.empty() ? name : nick; }
};

struct ChatLine {
    std::string name, text;
    cocos2d::ccColor3B color = {255, 255, 255};
    float age = 0.f;
    bool system = false;
};

struct Marker {  // pings
    cocos2d::CCPoint pos;
    std::string label, name;
    cocos2d::ccColor3B color;
    float age = 0.f;
};

struct Task {
    std::string id, text, by, claim;
    float rect[4] = {0, 0, 0, 0};
    bool done = false;
};
struct Vote {
    std::string id, q, by;
    int yes = 0, no = 0;
    bool open = true;
};
struct HistoryEntry {
    int q = 0;
    double t = 0;
    std::string name, tag;
    int add = 0, del = 0, upd = 0;
};
struct TemplateInfo {
    std::string id, name, by;
    int count = 0;
};
struct LeaderRow {
    std::string uid, name;
    int placed = 0, deleted = 0, chats = 0, objectsNow = 0, ach = 0;
    double timeMs = 0;
};
struct Drawing {
    std::string id, uid;
    cocos2d::ccColor3B color;
    float width = 3.f;
    std::vector<cocos2d::CCPoint> pts;
};
struct Reaction {
    std::string text;
    cocos2d::CCPoint pos;
    cocos2d::ccColor3B color;
    float age = 0.f;
};
struct ReplayOp {
    int o = 0; // 0 add, 1 upd, 2 del
    std::string id;
    float x = 0, y = 0;
    bool hasPos = false;
};
struct ReplayFrame {
    std::string author;
    std::vector<ReplayOp> ops;
};

struct HostOptions {
    std::string name, password, motd, tags;
    bool isPublic = true, approval = false;
};

struct RoomInfo {
    std::string code, name, host;
    int players = 0, objects = 0;
    bool locked = false, approval = false, finished = false;
    std::vector<std::string> tags;
};

class Session {
public:
    static Session& get();

    // ---- lifecycle
    void connect();
    void host(LevelEditorLayer* lel, HostOptions const& opts);
    void join(std::string const& code, std::string const& password);
    void requestRooms();
    void leave();
    void attachEditor(LevelEditorLayer* lel);
    void detachEditor(LevelEditorLayer* lel);

    bool inRoom() const { return !m_code.empty() && !m_pendingApproval; }
    bool active() const { return inRoom() && m_lel; }
    bool online() const;
    bool reconnecting() const { return m_wantRoom && !online(); }
    LevelEditorLayer* editor() const { return m_lel; }

    // ---- local edit hooks
    bool applyingRemote = false;
    void onLocalTouched(GameObject* obj);
    void onLocalRemoved(GameObject* obj);
    void requestFullSweep() { m_fastSweep = true; }

    // ---- per-frame
    void update(float dt);

    // ---- actions (UI)
    void sendChat(std::string const& text);
    void sendTyping(bool on);
    void sendPing(cocos2d::CCPoint world, std::string const& label);
    void setRole(int pid, std::string const& role);
    void kick(int pid, bool ban);
    void transferHost(int pid);
    void approve(int pid, bool ok);
    void setRoomOption(matjson::Value opts);
    void lockSelection(bool on);
    void setNick(std::string const& nick);
    void followPlayer(int pid) { m_followPid = pid; }
    void jumpToPlayer(int pid);
    void sendRaw(matjson::Value const& msg);

    // ---- tools (timeline, tasks, votes, templates...)
    void requestHistory(int before = 0);
    void restore(int seq);
    void undoUser(int pid, int n);
    void taskOp(std::string const& op, std::string const& id = "", std::string const& text = "");
    void voteOp(std::string const& op, std::string const& id = "", int v = 0, std::string const& q = "");
    void timerOp(std::string const& op);
    void requestGroups(int n);
    void saveTemplate(std::string const& name);
    void pasteTemplate(std::string const& id);
    void deleteTemplate(std::string const& id);
    void requestTemplates();
    void requestLeaderboard();
    void requestReplay(int from = 0);
    void sendReaction(std::string const& text);
    void sendDrawing(std::vector<cocos2d::CCPoint> const& pts);
    void clearDrawings(bool mine);
    void buildCreditsObject();
    double timerSeconds() const;
    cocos2d::ccColor3B colorForUid(std::string const& uid) const;
    cocos2d::CCRect viewRect() const;
    void goTo(cocos2d::CCPoint world);

    std::vector<Task> m_tasks;
    std::vector<Vote> m_votes;
    std::vector<HistoryEntry> m_history;
    std::vector<TemplateInfo> m_templates;
    std::vector<LeaderRow> m_leaderboard;
    std::vector<Drawing> m_drawings;
    std::vector<Reaction> m_reactions;
    std::vector<int> m_lastGroups;
    std::map<int, std::string> m_groupClaims;
    std::vector<ReplayFrame> m_replay;
    bool m_replayDone = false;
    bool m_timerRunning = false;
    double m_timerStart = 0, m_timerAcc = 0, m_clockOffset = 0;
    bool m_drawMode = false, m_heatmap = false;
    std::vector<cocos2d::CCPoint> m_currentStroke;
    std::function<void()> onData;   // open tool popups refresh on this

    // ---- state (read by UI)
    std::string m_code, m_roomName, m_motd, m_myRole = "builder";
    int m_myPid = 0;
    bool m_frozen = false, m_finished = false, m_pendingApproval = false;
    int m_slowMs = 0, m_rate = 400;
    bool m_roomPublic = true, m_roomApproval = false;
    std::unordered_map<int, Player> m_players;
    std::deque<ChatLine> m_chat;
    std::deque<ChatLine> m_feed;
    std::vector<Marker> m_markers;
    std::vector<RoomInfo> m_roomList;
    std::vector<std::pair<int, std::string>> m_joinRequests;
    std::unordered_set<std::string> m_locks;
    std::unordered_map<std::string, std::string> m_authorOf; // object id -> author uid (heatmap/credits)
    std::unordered_map<std::string, std::string> m_names;    // uid -> name
    int m_followPid = 0;
    int m_rtt = 0;
    int m_confettiPending = 0;
    bool m_joinIntoCurrent = false;   // load the room into the editor we're in (empty level)
    cocos2d::CCPoint lastTouch = {0, 0};
    std::function<void()> onRoomsUpdated;
    std::function<void()> onStateChanged;

    // object id lookups
    GameObject* objectFor(std::string const& id) const;
    std::string idFor(GameObject* obj) const;
    size_t objectCount() const { return m_objToId.size(); }

private:
    Session();
    void handle(matjson::Value const& m);
    void handleExtra(std::string const& t, matjson::Value const& m);
    void readExtras(matjson::Value const& m);
    void applyColors(std::string const& ls);
    void onOpen();
    void onClosed(std::string const& reason);
    void sendHello();
    void sendJoin();
    void doCreate();
    void enterRoom(matjson::Value const& m, bool isReset);
    void loadSnapshot(matjson::Value const& m);
    void applyOps(matjson::Value const& ops);
    void applyFix(matjson::Value const& fix);
    void applySettings(std::string const& s);
    std::string currentSettings();
    void readPlayer(Player& p, matjson::Value const& v);
    void addChat(std::string name, std::string text, cocos2d::ccColor3B color, bool system = false);
    void addFeed(std::string text);
    void sfx(char const* file);

    // sync engine
    std::string newId();
    std::string saveString(GameObject* obj);
    static std::string kindOf(GameObject* obj);
    void checkObject(GameObject* obj);
    void queueOp(char const* o, std::string const& id, std::string const* s, std::string const& kind);
    void flushOps();
    void sweepStep(int budget);
    void sendPresence();
    void sendPlaytest();
    GameObject* createRemote(std::string const& s);
    void removeRemote(GameObject* obj);
    void mapObject(GameObject* obj, std::string const& id, std::string const& s);
    void unmapObject(GameObject* obj);
    void clearMaps();

    LevelEditorLayer* m_lel = nullptr;
    std::string m_uid, m_prefix;
    uint64_t m_counter = 0;
    bool m_wantRoom = false;   // we want to be in m_code (used for auto-rejoin)
    bool m_isRejoin = false;
    bool m_hasJoined = false;
    bool m_wantList = false;
    std::string m_password;
    std::optional<matjson::Value> m_pendingSnapshot;
    std::optional<HostOptions> m_pendingHost;

    struct Rec { std::string id, s; };
    std::unordered_map<GameObject*, Rec> m_objToId;
    std::unordered_map<std::string, GameObject*> m_idToObj;
    std::unordered_set<GameObject*> m_touched;
    std::vector<matjson::Value> m_outOps;
    std::unordered_set<std::string> m_unsentIds;  // ids with ops not yet acknowledged (offline merge)
    std::map<int, std::vector<matjson::Value>> m_inflight;
    int m_batchN = 0;
    bool m_creatingRoom = false;
    double m_lastRejectNote = 0;

    size_t m_sweepIdx = 0;
    bool m_fastSweep = false;
    float m_flushTimer = 0, m_selTimer = 0, m_presTimer = 0, m_playTimer = 0, m_setTimer = 0, m_latTimer = 0;
    std::string m_lastSettings;
    std::vector<std::string> m_lastSel;
    bool m_wasPlaying = false;
    double m_latSentAt = 0;
};

}
