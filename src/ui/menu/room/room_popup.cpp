#include "room_popup.hpp"

#include "player_list_cell.hpp"
#include "room_join_popup.hpp"
#include <data/packets/all.hpp>
#include <net/network_manager.hpp>
#include <managers/error_queues.hpp>
#include <managers/profile_cache.hpp>
#include <managers/friend_list.hpp>
#include <managers/room.hpp>
#include <util/ui.hpp>

using namespace geode::prelude;

bool RoomPopup::setup() {
    auto& nm = NetworkManager::get();
    if (!nm.established()) {
        return false;
    }

    FriendListManager::get().maybeLoad();

    auto& rm = RoomManager::get();

    nm.addListener<RoomPlayerListPacket>([this](RoomPlayerListPacket* packet) {
        // log::debug("guys i received it");
        this->isWaiting = false;
        this->playerList = packet->data;
        this->sortPlayerList();
        auto& rm = RoomManager::get();
        bool changed = rm.roomId != packet->roomId;
        rm.setRoom(packet->roomId);
        this->onLoaded(changed || !roomBtnMenu);
    });

    nm.addListener<RoomJoinFailedPacket>([this](auto) {
        FLAlertLayer::create("Globed error", "Failed to join room: no room was found by the given ID.", "Ok")->show();
    });

    nm.addListener<RoomJoinedPacket>([this](auto) {
        this->reloadPlayerList(true);
    });

    nm.addListener<RoomCreatedPacket>([this](RoomCreatedPacket* packet) {
        auto ownData = ProfileCacheManager::get().getOwnData();
        auto* gjam = GJAccountManager::sharedState();

        this->playerList = {PlayerRoomPreviewAccountData(
            gjam->m_accountID,
            GameManager::get()->m_playerUserID.value(),
            gjam->m_username,
            ownData.cube,
            ownData.color1,
            ownData.color2,
            ownData.glowColor,
            0
        )};

        RoomManager::get().setRoom(packet->roomId);
        this->onLoaded(true);
    });

    auto listview = ListView::create(CCArray::create(), PlayerListCell::CELL_HEIGHT, LIST_WIDTH, LIST_HEIGHT);
    listLayer = GJCommentListLayer::create(listview, "", {192, 114, 62, 255}, LIST_WIDTH, LIST_HEIGHT, false);

    float xpos = (m_mainLayer->getScaledContentSize().width - LIST_WIDTH) / 2;
    listLayer->setPosition({xpos, 85.f});
    m_mainLayer->addChild(listLayer);

    this->reloadPlayerList();

    Build<CCSprite>::createSpriteName("GJ_updateBtn_001.png")
        .scale(0.9f)
        .intoMenuItem([this](auto) {
            this->reloadPlayerList(true);
        })
        .pos(m_size.width / 2.f - 3.f, -m_size.height / 2.f + 3.f)
        .id("reload-btn"_spr)
        .intoNewParent(CCMenu::create())
        .parent(m_mainLayer);

    return true;
}

void RoomPopup::onLoaded(bool stateChanged) {
    this->removeLoadingCircle();

    auto cells = CCArray::create();

    for (const auto& pdata : playerList) {
        auto* cell = PlayerListCell::create(pdata);
        cells->addObject(cell);
    }

    // preserve scroll position
    float scrollPos = util::ui::getScrollPos(listLayer->m_list);
    int previousCellCount = listLayer->m_list->m_entries->count();

    listLayer->m_list->removeFromParent();
    listLayer->m_list = Build<ListView>::create(cells, PlayerListCell::CELL_HEIGHT, LIST_WIDTH, LIST_HEIGHT)
        .parent(listLayer)
        .collect();

    if (previousCellCount != 0 && !stateChanged) {
        util::ui::setScrollPos(listLayer->m_list, scrollPos);
    }

    if (stateChanged) {
        auto& rm = RoomManager::get();
        this->setTitle(rm.isInGlobal() ? "Global room" : fmt::format("Room {}", rm.roomId.load()));
        this->addButtons();
    }
}

void RoomPopup::addButtons() {
    // remove existing buttons
    if (roomBtnMenu) roomBtnMenu->removeFromParent();

    auto& rm = RoomManager::get();
    float popupCenter = CCDirector::get()->getWinSize().width / 2;

    if (rm.isInGlobal()) {
        Build<CCMenu>::create()
            .layout(
                RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setGap(5.0f)
            )
            .id("btn-menu"_spr)
            .pos(popupCenter, 55.f)
            .parent(m_mainLayer)
            .store(roomBtnMenu);

        Build<ButtonSprite>::create("Join room", "bigFont.fnt", "GJ_button_01.png", 0.8f)
            .intoMenuItem([this](auto) {
                if (!this->isLoading()) {
                    RoomJoinPopup::create()->show();
                }
            })
            .id("join-room-btn"_spr)
            .parent(roomBtnMenu);

        Build<ButtonSprite>::create("Create room", "bigFont.fnt", "GJ_button_01.png", 0.8f)
            .intoMenuItem([this](auto) {
                if (!this->isLoading()) {
                    NetworkManager::get().send(CreateRoomPacket::create());
                    this->reloadPlayerList(false);
                }
            })
            .id("create-room-btn"_spr)
            .parent(roomBtnMenu);

        roomBtnMenu->updateLayout();
    } else {
        Build<ButtonSprite>::create("Leave room", "bigFont.fnt", "GJ_button_01.png", 0.8f)
            .intoMenuItem([this](auto) {
                if (!this->isLoading()) {
                    NetworkManager::get().send(LeaveRoomPacket::create());
                    this->reloadPlayerList(false);
                }
            })
            .id("leave-room-btn"_spr)
            .intoNewParent(CCMenu::create())
            .id("leave-room-btn-menu"_spr)
            .pos(popupCenter, 55.f)
            .parent(m_mainLayer)
            .store(roomBtnMenu);
    }
}

void RoomPopup::removeLoadingCircle() {
    if (loadingCircle) {
        loadingCircle->fadeAndRemove();
        loadingCircle = nullptr;
    }
}

void RoomPopup::reloadPlayerList(bool sendPacket) {
    auto& nm = NetworkManager::get();
    if (!nm.established()) {
        this->onClose(this);
        return;
    }

    // remove loading circle
    this->removeLoadingCircle();

    // send the request
    if (sendPacket) {
        // log::debug("requesting player list");
        if (!isWaiting) {
            NetworkManager::get().send(RequestRoomPlayerListPacket::create());
            isWaiting = true;
        }
    }

    // show the circle
    loadingCircle = LoadingCircle::create();
    loadingCircle->setParentLayer(listLayer);
    loadingCircle->setPosition(-listLayer->getPosition());
    loadingCircle->show();
}

bool RoomPopup::isLoading() {
    return loadingCircle != nullptr;
}

void RoomPopup::sortPlayerList() {
    auto& flm = FriendListManager::get();

    // filter out the weird people (old game server used to send unauthenticated people too)
    playerList.erase(std::remove_if(playerList.begin(), playerList.end(), [](const auto& player) {
        return player.id == 0;
    }), playerList.end());

    // show friends before everyone else, and sort everyone alphabetically by the name
    std::sort(playerList.begin(), playerList.end(), [&flm](const auto& p1, const auto& p2) -> bool {
        bool isFriend1 = flm.isFriend(p1.id);
        bool isFriend2 = flm.isFriend(p2.id);

        if (isFriend1 != isFriend2) {
            return isFriend1;
        } else {
            // convert both names to lowercase
            std::string name1 = p1.name, name2 = p2.name;
            std::transform(name1.begin(), name1.end(), name1.begin(), ::tolower);
            std::transform(name2.begin(), name2.end(), name2.begin(), ::tolower);

            // sort alphabetically
            return name1 < name2;
        }
    });
}

RoomPopup::~RoomPopup() {
    auto& nm = NetworkManager::get();

    nm.removeListener<RoomPlayerListPacket>();
    nm.removeListener<RoomJoinedPacket>();
    nm.removeListener<RoomJoinFailedPacket>();
    nm.removeListener<RoomCreatedPacket>();

    nm.suppressUnhandledFor<RoomPlayerListPacket>(util::time::seconds(1));
    nm.suppressUnhandledFor<RoomJoinedPacket>(util::time::seconds(1));
    nm.suppressUnhandledFor<RoomJoinFailedPacket>(util::time::seconds(1));
    nm.suppressUnhandledFor<RoomCreatedPacket>(util::time::seconds(1));
}

RoomPopup* RoomPopup::create() {
    auto ret = new RoomPopup;
    if (ret->init(POPUP_WIDTH, POPUP_HEIGHT)) {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}