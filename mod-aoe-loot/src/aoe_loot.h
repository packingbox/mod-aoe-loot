#ifndef MODULE_AOELOOT_H
#define MODULE_AOELOOT_H

#include "ScriptMgr.h"
#include "Config.h"
#include "Chat.h"
#include "Player.h"
#include "ScriptedGossip.h"

enum AoeLootString
{
    AOE_ACORE_STRING_MESSAGE = 50000,
    AOE_ITEM_IN_THE_MAIL
};

class AOELootPlayer : public PlayerScript
{
public:
    AOELootPlayer() : PlayerScript("AOELootPlayer") { }

    void OnPlayerLogin(Player* player) override;
};

class AOELootServer : public ServerScript
{
public:
    AOELootServer() : ServerScript("AOELootServer") { }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override;
};

void AddSC_AoeLoot()
{
    new AOELootPlayer();
    new AOELootServer();
}

#endif //MODULE_AOELOOT_H