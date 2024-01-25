#include "settings_layer.hpp"

#include <UIBuilder.hpp>

#include <util/ui.hpp>
#include <managers/settings.hpp>

using namespace geode::prelude;

bool GlobedSettingsLayer::init() {
    if (!CCLayer::init()) return false;

    auto winsize = CCDirector::get()->getWinSize();

    auto listview = Build<ListView>::create(createSettingsCells(), GlobedSettingCell::CELL_HEIGHT, LIST_WIDTH, LIST_HEIGHT)
        .collect();

    Build<GJListLayer>::create(listview, "Settings", ccc4(0, 0, 0, 180), LIST_WIDTH, 220.f, 0)
        .zOrder(2)
        .anchorPoint(0.f, 0.f)
        .parent(this)
        .id("setting-list"_spr)
        .store(listLayer);

    listLayer->setPosition({winsize / 2 - listLayer->getScaledContentSize() / 2});

    util::ui::addBackground(this);

    auto menu = CCMenu::create();
    this->addChild(menu);
    util::ui::addBackButton(menu, util::ui::navigateBack);

    this->setKeyboardEnabled(true);
    this->setKeypadEnabled(true);

    return true;
}

#define MAKE_SETTING(cat, name, setname, setdesc) cells->addObject(GlobedSettingCell::create(&settings.cat.name, getCellType<decltype(settings.cat.name)>(), setname, setdesc))
#define MAKE_SETTING_TYPE(cat, name, type, setname, setdesc) cells->addObject(GlobedSettingCell::create(&settings.cat.name, type, setname, setdesc))
#define MAKE_HEADER(name) cells->addObject(GlobedSettingHeaderCell::create(name))

CCArray* GlobedSettingsLayer::createSettingsCells() {
    using Type = GlobedSettingCell::Type;

    auto cells = CCArray::create();

    auto& settings = GlobedSettings::get();

    MAKE_HEADER("Globed");
    MAKE_SETTING(globed, autoconnect, "Autoconnect", "Automatically connect to the last connected server on launch.");
    MAKE_SETTING(globed, tpsCap, "TPS cap", "Maximum amount of packets per second sent between the client and the server. Useful only for very silly things.");

    MAKE_HEADER("Overlay");
    MAKE_SETTING(overlay, enabled, "Ping overlay", "Show a small overlay when in a level, displaying the current latency to the server.");
    MAKE_SETTING(overlay, opacity, "Overlay opacity", "Opacity of the displayed overlay.");
    MAKE_SETTING(overlay, hideConditionally, "Hide conditionally", "Hide the ping overlay when not connected to a server or in a non-uploaded level, instead of showing a substitute message.");

    MAKE_HEADER("Communication");
    MAKE_SETTING(communication, voiceEnabled, "Voice chat", "Enables in-game voice chat.");
    MAKE_SETTING(communication, lowerAudioLatency, "Lower audio latency", "Decreases the audio buffer size by 2 times, reducing the latency but potentially causing audio issues.");
    MAKE_SETTING_TYPE(communication, audioDevice, Type::AudioDevice, "Audio device", "The input device used for recording your voice.");

    return cells;
}

GlobedSettingsLayer* GlobedSettingsLayer::create() {
    auto ret = new GlobedSettingsLayer;
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

void GlobedSettingsLayer::scene() {
    if (auto* layer = create()) {
        auto scene = CCScene::create();
        layer->setPosition(0.f, 0.f);
        scene->addChild(layer);

        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    }
}