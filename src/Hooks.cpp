// Multiplayer Editor — made by x7wv
#include "Session.hpp"
#include "UI.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;
using me::Session;

// ---------------------------------------------------------------- level editor
class $modify(MELevelEditorLayer, LevelEditorLayer) {
    struct Fields {
        LevelEditorLayer* self = nullptr;
        ~Fields() { if (self) Session::get().detachEditor(self); }
    };

    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (!noUI) {
            m_fields->self = this;
            Session::get().attachEditor(this);
        }
        return true;
    }

    GameObject* createObject(int key, CCPoint position, bool noUndo) {
        auto obj = LevelEditorLayer::createObject(key, position, noUndo);
        if (Session::get().editor() == this) Session::get().onLocalTouched(obj);
        return obj;
    }

    CCArray* createObjectsFromString(gd::string const& str, bool noUndo, bool noLimit) {
        auto arr = LevelEditorLayer::createObjectsFromString(str, noUndo, noLimit);
        auto& s = Session::get();
        if (arr && !s.applyingRemote && s.active() && s.editor() == this) {
            for (size_t i = 0; i < arr->count(); i++) s.onLocalTouched(static_cast<GameObject*>(arr->objectAtIndex(i)));
        }
        return arr;
    }

    void removeObject(GameObject* object, bool noUndo) {
        if (Session::get().editor() == this) Session::get().onLocalRemoved(object);
        LevelEditorLayer::removeObject(object, noUndo);
    }
};

// ---------------------------------------------------------------- editor ui
class $modify(MEEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (auto overlay = me::Overlay::create(editorLayer)) this->addChild(overlay, 1000);
        return true;
    }

    void undoLastAction(CCObject* sender) {
        EditorUI::undoLastAction(sender);
        Session::get().requestFullSweep();
    }

    void redoLastAction(CCObject* sender) {
        EditorUI::redoLastAction(sender);
        Session::get().requestFullSweep();
    }

    CCPoint toWorld(CCTouch* touch) {
        return m_editorLayer->m_objectLayer->convertToNodeSpace(touch->getLocation());
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto& s = Session::get();
        s.lastTouch = touch->getLocation();
        s.followPlayer(0); // touching the screen takes the camera back
        if (s.m_drawMode && s.active()) {
            // only draw on the level area, buttons still work through their own menus
            s.m_currentStroke.clear();
            s.m_currentStroke.push_back(toWorld(touch));
            return true;
        }
        return EditorUI::ccTouchBegan(touch, event);
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        auto& s = Session::get();
        s.lastTouch = touch->getLocation();
        if (s.m_drawMode && !s.m_currentStroke.empty()) {
            auto p = toWorld(touch);
            float minDist = 4.f / std::max(.1f, m_editorLayer->m_objectLayer->getScale());
            if (p.getDistance(s.m_currentStroke.back()) > minDist && s.m_currentStroke.size() < 900) s.m_currentStroke.push_back(p);
            return;
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        auto& s = Session::get();
        if (s.m_drawMode && !s.m_currentStroke.empty()) {
            s.m_currentStroke.push_back(toWorld(touch));
            s.sendDrawing(s.m_currentStroke);
            s.m_currentStroke.clear();
            return;
        }
        EditorUI::ccTouchEnded(touch, event);
    }
};

// ---------------------------------------------------------------- pause menu
class $modify(MEEditorPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorPauseLayer::init(layer)) return false;
        auto spr = ButtonSprite::create("Multiplayer", "goldFont.fnt", "GJ_button_05.png", .8f);
        spr->setScale(.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MEEditorPauseLayer::onMultiplayer));
        btn->setID("multiplayer-button"_spr);
        if (auto menu = this->getChildByID("resume-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto m = CCMenu::create();
            auto win = CCDirector::get()->getWinSize();
            m->setPosition({win.width / 2, 30});
            m->addChild(btn);
            this->addChild(m, 10);
        }
        return true;
    }

    void onMultiplayer(CCObject*) { me::openMainPopup(); }

    // leave the room *before* the editor tears down, so nothing gets sent as deleted
    void onExitEditor(CCObject* sender) { Session::get().detachEditor(m_editorLayer); EditorPauseLayer::onExitEditor(sender); }
    void onExitNoSave(CCObject* sender) { Session::get().detachEditor(m_editorLayer); EditorPauseLayer::onExitNoSave(sender); }
    void onSaveAndExit(CCObject* sender) { Session::get().detachEditor(m_editorLayer); EditorPauseLayer::onSaveAndExit(sender); }
    void onSaveAndPlay(CCObject* sender) { Session::get().detachEditor(m_editorLayer); EditorPauseLayer::onSaveAndPlay(sender); }
};

// ---------------------------------------------------------------- main menu + credits
class $modify(MEMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto spr = CircleButtonSprite::create(CCLabelBMFont::create("MP", "bigFont.fnt"), CircleBaseColor::DarkPurple, CircleBaseSize::MediumAlt);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MEMenuLayer::onMultiplayer));
        btn->setID("multiplayer-button"_spr);
        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }

        if (!Mod::get()->getSavedValue<bool>("shown-credits", false)) {
            Mod::get()->setSavedValue("shown-credits", true);
            Loader::get()->queueInMainThread([] {
                FLAlertLayer::create("Multiplayer Editor",
                    "Thanks for installing <cp>Multiplayer Editor</c>!\n"
                    "Made by <cy>x7wv</c>.\n\n"
                    "Set your server in the mod settings, then open a level and press <cg>MP</c> to host, or press <cg>MP</c> here to join.",
                    "Let's go")->show();
            });
        }
        return true;
    }

    void onMultiplayer(CCObject*) { me::openMainPopup(); }
};

$on_mod(Loaded) {
    log::info("Multiplayer Editor loaded - made by x7wv");
}
