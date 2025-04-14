#include "aoe_loot.h"
#include "ScriptMgr.h"
#include "World.h"
#include "LootMgr.h"
#include "ServerScript.h"
#include "WorldSession.h"
#include "WorldPacket.h" 
#include "Player.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandArgs.h"
#include "WorldObjectScript.h"
#include "Creature.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include <fmt/format.h>
#include "Corpse.h"


using namespace Acore::ChatCommands;

bool AOELootServer::CanPacketReceive(WorldSession* session, WorldPacket& packet)
{
    if (packet.GetOpcode() == CMSG_LOOT)
    {
        Player* player = session->GetPlayer();
        if (player)
        {
            /*
            // Check if SHIFT key is pressed - if so, bypass AOE loot
            if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_MODIFIER_ACTIVE))
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Bypassing AOE loot because SHIFT key is held");
                    ChatHandler(player->GetSession()).PSendSysMessage("AOE Loot: SHIFT key detected - bypassing AOE loot");
                }
                return true; // Let the normal loot handling proceed
            }
            */
            ChatHandler handler(player->GetSession());
            handler.ParseCommands(".startaoeloot");
        }
    }
    return true;
}

ChatCommandTable AoeLootCommandScript::GetCommands() const
{
    static ChatCommandTable playerAoeLootCommandTable =
    {
        { "startaoeloot", HandleStartAoeLootCommand, SEC_PLAYER, Console::No }
    };
    return playerAoeLootCommandTable;
}

bool AoeLootCommandScript::HandleStartAoeLootCommand(ChatHandler* handler, Optional<std::string> /*args*/)
{
    if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
    return true;

    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
    return true;

    float range = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0);

    std::list<Creature*> nearbyCorpses;
    player->GetDeadCreatureListInGrid(nearbyCorpses, range);

    uint32 totalItemsLooted = 0;
    uint32 totalCopperLooted = 0;
    uint32 totalCorpsesLooted = 0;

    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range);
        handler->PSendSysMessage(fmt::format("AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range));
    }

    // Process each corpse one by one
    for (auto* creature : nearbyCorpses)
    {
        if (!player || !creature || !player->IsInWorld())
        {
            continue;
        }

        if (!creature->hasLootRecipient() || !creature->isTappedBy(player)) 
        {
            // Not a valid loot target for this player
            continue;
        }

        // Skip if corpse is not valid for looting
        if (!creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
        {
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - not lootable", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - not lootable", creature->GetGUID().ToString()));
            }
            continue;
        }

        // Additional permission check
        if (player->GetMap()->Instanceable())
        {
            if(!player->isAllowedToLoot(creature))
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - player not allowed to loot", creature->GetGUID().ToString());
                    handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - player not allowed to loot", creature->GetGUID().ToString()));
                }
                continue;
            }
        }
        
        // Double-check distance to prevent exploits
        if (player->GetDistance(creature) > range)
        {
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - out of range", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - out of range", creature->GetGUID().ToString()));
            }
            continue;
        }
        
        // Set this corpse as the current loot target
        player->SetLootGUID(creature->GetGUID());
        
        Loot* loot = &creature->loot;
        
        if (!loot)
        {
            continue;
        }
    
        // Process regular items
        for (uint8 lootSlot = 0; lootSlot < loot->items.size(); ++lootSlot)
        {

            // Store the loot item in the player's inventory
            InventoryResult msg;
            player->StoreLootItem(lootSlot, loot, msg);

            // Debug
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalItemsLooted++;
                
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Successfully looted item from slot {} (ID: {}) from {}", lootSlot, loot->items[lootSlot].itemid, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Successfully looted item from slot {} (ID: {})", lootSlot, loot->items[lootSlot].itemid));
            }
        }

        // Process quest items
        if (!loot->quest_items.empty())
        {
            // Calculate starting slot for quest items
            uint8 firstQuestSlot = loot->items.size();
            
            // Try to loot each potential quest slot
            for (uint8 i = 0; i < loot->quest_items.size(); ++i)
            {
                uint8 lootSlot = firstQuestSlot + i;
                
                // Store the loot item in the player's inventory
                InventoryResult msg;
                player->StoreLootItem(lootSlot, loot, msg);

                // Debug logging
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    totalItemsLooted++;
                    
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Successfully looted quest item from slot {}", lootSlot);
                    handler->PSendSysMessage(fmt::format("AOE Loot: Successfully looted quest item from slot {}", lootSlot));
                    
                }
            }
        }

        // Handle money with direct packet
        if (loot->gold > 0)
        {
            WorldPacket moneyPacket(CMSG_LOOT_MONEY, 0);
            player->GetSession()->HandleLootMoneyOpcode(moneyPacket);
            
            // Debug
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalCopperLooted += loot->gold;
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Looted {} copper from {}", loot->gold, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} copper from {}", loot->gold, creature->GetGUID().ToString()));
            }     
        }

        // Check if the corpse is now fully looted
        if (loot->isLooted())
        {
            // Cleanup the loot object from the corpse
            creature->AllLootRemovedFromCorpse();
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }

        
        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
        {
            LOG_DEBUG("module.aoe_loot", "AOE Loot: Summary - Looted {} items, {} gold from {} corpses", totalItemsLooted, totalCopperLooted, totalCorpsesLooted);
            handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} items and {} gold from {} corpses", totalItemsLooted, totalCopperLooted, totalCorpsesLooted)); 
        }
    }
    return true;
}


// Copied from sudlud's "mod-aoe-loot" module.
void AOELootPlayer::OnPlayerLogin(Player* player)
{
    if (sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
    {
        if (sConfigMgr->GetOption<bool>("AOELoot.Message", true))
            ChatHandler(player->GetSession()).PSendSysMessage(AOE_ACORE_STRING_MESSAGE);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AddSC_AoeLoot()
{
    new AOELootPlayer();
    new AOELootServer();
    new AoeLootCommandScript();
}
