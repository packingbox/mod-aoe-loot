#ifndef MODULE_AOELOOT_H
#define MODULE_AOELOOT_H

#include "ScriptMgr.h"
#include "Config.h"
#include "ServerScript.h"
#include "Chat.h"
#include "Player.h"
#include "Item.h"
#include "ScriptedGossip.h"
#include "ChatCommand.h"       
#include "ChatCommandArgs.h" 
#include "AccountMgr.h"
#include <vector> 
#include <list>
#include <map>
#include <ObjectGuid.h>

using namespace Acore::ChatCommands;

class AoeLootServer : public ServerScript
{
public:
    AoeLootServer() : ServerScript("AoeLootServer") {}

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override;
};

class AoeLootPlayer : public PlayerScript
{
public:
    AoeLootPlayer() : PlayerScript("AoeLootPlayer") {}

    void OnPlayerLogin(Player* player) override;

};

class AoeLootCommandScript : public CommandScript
{
public:
    AoeLootCommandScript() : CommandScript("AoeLootCommandScript") {}
    ChatCommandTable GetCommands() const override;

    static bool HandleAoeLootOnCommand(ChatHandler* handler, Optional<std::string> args);
    static bool HandleAoeLootOffCommand(ChatHandler* handler, Optional<std::string> args);
    static bool HandleStartAoeLootCommand(ChatHandler* handler, Optional<std::string> args);
    static bool ProcessLootSlot(Player* player, ObjectGuid lguid, uint8 lootSlot);
    static bool ProcessLootMoney(Player* player, Creature* creature);
    static void ProcessLootRelease(ObjectGuid lguid, Player* player, Loot* loot);
    
};

void AddSC_AoeLoot();


#endif //MODULE_AOELOOT_H
