#pragma once
#include <defs.hpp>

#include <data/packets/client/admin.hpp>

class AdminPopup : public geode::Popup<> {
public:
    static constexpr float POPUP_WIDTH = 400.f;
    static constexpr float POPUP_HEIGHT = 280.f;

    static AdminPopup* create();

private:
    geode::InputNode* passwordInput = nullptr;

    geode::InputNode
        *messageInput = nullptr,
        *playerInput = nullptr,
        *roomIdInput = nullptr,
        *levelIdInput = nullptr,
        *disconnectUserInput = nullptr;

    bool setup() override;
    void commonSend(AdminSendNoticeType type);
};
