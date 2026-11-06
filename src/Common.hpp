// Multiplayer Editor — made by x7wv
// Shared UI helpers
#pragma once
#include "UI.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>

namespace me {
using namespace geode::prelude;
using geode::cocos::CCMenuItemExt;

inline ccColor4F c4f(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

inline CCMenuItemSpriteExtra* textButton(char const* text, geode::Function<void(CCMenuItemSpriteExtra*)> cb, float scale = .6f, char const* bg = "GJ_button_01.png") {
    auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, .8f);
    spr->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
}

inline CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color = {255, 255, 255}) {
    auto l = CCLabelBMFont::create(text.c_str(), font);
    l->setScale(scale);
    l->setColor(color);
    return l;
}

inline ccColor3B roleColor(std::string const& role) {
    if (role == "host") return {255, 215, 0};
    if (role == "builder") return {120, 255, 120};
    if (role == "decorator") return {120, 200, 255};
    return {180, 180, 180};
}

// ---------------------------------------------------------------- popup base
class BasePopup : public Popup {
protected:
    CCMenu* m_menu2 = nullptr;
    bool setup(float w, float h, char const* title) {
        if (!Popup::init(w, h)) return false;
        this->setTitle(title);
        m_menu2 = CCMenu::create();
        m_menu2->setPosition({0, 0});
        m_menu2->setContentSize(m_size);
        m_mainLayer->addChild(m_menu2, 5);
        return true;
    }
    CCMenuItemSpriteExtra* button(char const* text, CCPoint pos, geode::Function<void(CCMenuItemSpriteExtra*)> cb, float scale = .7f, char const* bg = "GJ_button_01.png") {
        auto b = textButton(text, std::move(cb), scale, bg);
        b->setPosition(pos);
        m_menu2->addChild(b);
        return b;
    }
    CCLabelBMFont* text(std::string const& t, CCPoint pos, float scale = .5f, char const* font = "bigFont.fnt", ccColor3B col = {255, 255, 255}) {
        auto l = label(t, font, scale, col);
        l->setPosition(pos);
        m_mainLayer->addChild(l, 5);
        return l;
    }
    TextInput* input(char const* placeholder, CCPoint pos, float width, std::string const& value = "") {
        auto in = TextInput::create(width, placeholder);
        in->setPosition(pos);
        if (!value.empty()) in->setString(value);
        m_mainLayer->addChild(in, 5);
        return in;
    }
    CCMenuItemToggler* toggle(char const* name, CCPoint pos, bool on, geode::Function<void(bool)> cb) {
        auto t = CCMenuItemExt::createTogglerWithStandardSprites(.6f, [cb = std::move(cb)](CCMenuItemToggler* t) mutable { cb(!t->isToggled()); });
        t->toggle(on);
        t->setPosition(pos);
        m_menu2->addChild(t);
        auto l = label(name, "bigFont.fnt", .35f);
        l->setAnchorPoint({0, .5f});
        l->setPosition(pos + CCPoint{14, 0});
        m_mainLayer->addChild(l, 5);
        return t;
    }
};


}
