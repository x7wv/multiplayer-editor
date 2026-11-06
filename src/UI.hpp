// Multiplayer Editor — made by x7wv
#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <unordered_map>

namespace me {

struct Theme {
    cocos2d::ccColor3B accent, accent2, text;
    cocos2d::ccColor4B panel;
};
Theme const& theme();

// Lives on top of the editor: status bar, buttons, cursors, selections,
// playtest ghosts, pings, chat, activity feed, confetti.
class Overlay : public cocos2d::CCNode {
public:
    static Overlay* create(LevelEditorLayer* lel);
    void update(float dt) override;
    void refreshButtons();

protected:
    bool initOverlay(LevelEditorLayer* lel);
    cocos2d::CCPoint toScreen(cocos2d::CCPoint world);
    void drawSelections();
    void updateCursors();
    void updateGhosts();
    void updateMarkers();
    void updateChat();
    void spawnConfetti();
    void drawTools();
    void updateToolLabels();

    LevelEditorLayer* m_lel = nullptr;
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_credit = nullptr;
    cocos2d::CCLabelBMFont* m_typing = nullptr;
    cocos2d::CCNode* m_chatNode = nullptr;
    cocos2d::CCNode* m_feedNode = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNode* m_roomButtons = nullptr;
    std::unordered_map<int, cocos2d::CCNode*> m_cursors;
    std::unordered_map<int, SimplePlayer*> m_ghosts;
    cocos2d::CCNode* m_markerNode = nullptr;
    cocos2d::CCNode* m_toolLabels = nullptr;
    cocos2d::CCLabelBMFont* m_timer = nullptr;
    cocos2d::CCNode* m_heatLegend = nullptr;
    size_t m_lastChatSize = 0;
    float m_chatAge = 0;
    float m_statusTimer = 0;
};

void openMainPopup();
void openHostPopup();
void openJoinPopup(std::string const& code = "", bool needsPassword = false);
void openBrowserPopup();
void openPlayersPopup();
void openChatPopup();
void openRoomSettingsPopup();
void openCreditsPopup();
void openToolsPopup();

}
