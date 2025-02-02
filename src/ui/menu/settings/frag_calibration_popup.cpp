#include "frag_calibration_popup.hpp"

#include <data/packets/client/connection.hpp>
#include <net/network_manager.hpp>
#include <managers/settings.hpp>
#include <util/crypto.hpp>
#include <util/rng.hpp>

using namespace geode::prelude;

bool FragmentationCalibartionPopup::setup() {
    this->setTitle("Connection test");

    auto winSize = CCDirector::get()->getWinSize();

    Build<CCLabelBMFont>::create("", "bigFont.fnt")
        .pos(winSize.width / 2, winSize.height / 2 + m_size.height / 2 - 50.f)
        .scale(0.3f)
        .parent(m_mainLayer)
        .store(statusLabel);

    auto& nm = NetworkManager::get();
    nm.addListener<ConnectionTestResponsePacket>([this] (ConnectionTestResponsePacket* packet) {
        if (packet->uid != this->uid) {
            FLAlertLayer::create("Error", "Test failed. Server sent invalid unique packet ID.", "Ok")->show();
            this->onClose(this);
            return;
        }
        // discard the data

        this->failedAttempts = 0;
        this->nextStep();
    });

    currentSizeIdx = 0;
    currentSize = TEST_PACKET_SIZES[currentSizeIdx];

    this->nextStep();

    this->schedule(schedule_selector(FragmentationCalibartionPopup::checkForUpdates), 0.25f);

    return true;
}

void FragmentationCalibartionPopup::checkForUpdates(float) {
    if (util::time::systemNow() - lastPacket > util::time::millis(2500)) {
        currentAttempt--;
        failedAttempts++;
        log::warn("failed attempt! size: {}, current attempt: {}", currentSize, currentAttempt);

        // if no response 3 times in a row, abort
        if (failedAttempts > 2) {
            auto newFragLimit = TEST_PACKET_SIZES[currentSizeIdx - 1];
            auto& settings = GlobedSettings::get();
            settings.globed.fragmentationLimit = newFragLimit;
            settings.save();
            FLAlertLayer::create("Notice", fmt::format("Test failed with packet size = {}. Setting the maximum packet size to {}. Reconnect to the server in order to see any change.", currentSize, newFragLimit), "Ok")->show();
            this->closeDelayed();
            return;
        }

        this->nextStep();
    }
}

void FragmentationCalibartionPopup::nextStep() {
    if (currentAttempt > 4) {
        currentAttempt = 0;
        auto newSize = TEST_PACKET_SIZES[++currentSizeIdx];
        currentSize = newSize;
    }

    if (currentSize == 0) {
        // calibration was fully successful, user can send up to 64kb
        auto& settings = GlobedSettings::get();
        settings.globed.fragmentationLimit = 65000;
        settings.save();

        FLAlertLayer::create("Success", "Connection test succeeded. No issues found.", "Ok")->show();
        this->closeDelayed();
        return;
    }

    auto& rng = util::rng::Random::get();
    uid = rng.generate<int>();

    auto data = util::crypto::secureRandom(currentSize);

    auto& nm = NetworkManager::get();
    nm.send(ConnectionTestPacket::create(uid, std::move(data)));
    lastPacket = util::time::systemNow();

    currentAttempt++;

    statusLabel->setString(fmt::format("Attempt {} with size = {}", currentAttempt, currentSize).c_str());
}

void FragmentationCalibartionPopup::onClose(cocos2d::CCObject* obj) {
    Popup::onClose(obj);

    auto& nm = NetworkManager::get();
    nm.removeListener<ConnectionTestResponsePacket>();
    nm.suppressUnhandledFor<ConnectionTestResponsePacket>(util::time::seconds(1));
}

void FragmentationCalibartionPopup::closeDelayed() {
    // this is needed because we cant call NetworkManager::removeListener inside of a listener callback.
    Loader::get()->queueInMainThread([this] {
        this->onClose(this);
    });
}

FragmentationCalibartionPopup* FragmentationCalibartionPopup::create() {
    auto ret = new FragmentationCalibartionPopup;
    if (ret->init(POPUP_WIDTH, POPUP_HEIGHT)) {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}