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

enum AoeLootString
{
    AOE_ACORE_STRING_MESSAGE = 50000
};

class AOELootServer : public ServerScript
{
public:
    AOELootServer() : ServerScript("AOELootServer") {}

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override;
};

class AOELootPlayer : public PlayerScript
{
public:
    AOELootPlayer() : PlayerScript("AOELootPlayer") {}

    void OnPlayerLogin(Player* player) override;

};

class AoeLootCommandScript : public CommandScript
{
public:
    AoeLootCommandScript() : CommandScript("AoeLootCommandScript") {}
    ChatCommandTable GetCommands() const override;

    static bool HandleStartAoeLootCommand(ChatHandler* handler, Optional<std::string> args);
    
};

void AddSC_AoeLoot();


#endif //MODULE_AOELOOT_H
