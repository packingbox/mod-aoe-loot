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

using namespace Acore::ChatCommands;

bool AOELootServer::CanPacketReceive(WorldSession* session, WorldPacket& packet)
{
    if (packet.GetOpcode() == CMSG_LOOT)
    {
        Player* player = session->GetPlayer();
        if (player)
        {
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
    uint32 totalSkippedItems = 0;

    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range);
        handler->PSendSysMessage(fmt::format("AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range));
    }
    
    for (auto* creature : nearbyCorpses)
    {
        if (!player || !creature || !player->IsInWorld())
            continue;

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
        if (!player->GetMap()->Instanceable() && !player->isAllowedToLoot(creature))
        {
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - player not allowed to loot", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - player not allowed to loot", creature->GetGUID().ToString()));
            }
            continue;
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
        
        // Set loot GUID to current creature
        player->SetLootGUID(creature->GetGUID());
        Loot* loot = &creature->loot;
        
        uint32 maxSlots = loot->GetMaxSlotInLootFor(player);

        uint32 itemsLootedFromCorpse = 0;

        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
        {
            LOG_DEBUG("module.aoe_loot", "AOE Loot: Processing creature {} with {} slots", creature->GetGUID().ToString(), maxSlots);
            handler->PSendSysMessage(fmt::format("AOE Loot: Processing creature {} with {} slots", creature->GetGUID().ToString(), maxSlots));
        }

        // Process items - first pass for quest items (priority)
        for (uint32 i = 0; i < maxSlots; ++i)
        {
            LootItem* lootItem = loot->LootItemInSlot(i, player);

            if (!lootItem)
                continue;
                
            // Check if player can loot this item
            if (!lootItem->AllowedForPlayer(player, creature->GetGUID()))
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Item in slot {} not allowed for player - skipping", i);
                    handler->PSendSysMessage(fmt::format("AOE Loot: Item in slot {} not allowed for player - skipping", i));
                }
                continue;
            }
            
            // Skip items that are currently being rolled on in group
            if (lootItem->is_blocked)
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    totalSkippedItems++;

                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Item in slot {} is currently being rolled on - skipping", i);
                    handler->PSendSysMessage(fmt::format("AOE Loot: Item in slot {} is currently being rolled on - skipping", i));
                }
                continue;
            }
                
            // First pass - only process quest items
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(lootItem->itemid);

            if (!itemTemplate || !(itemTemplate->Class == ITEM_CLASS_QUEST))
                continue;
                
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Attempting to loot quest item ID: {} Count: {} Slot: {} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Attempting to loot quest item ID: {} Count: {} Slot: {} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetGUID().ToString()));
            }
            
            // Store the loot item in the player's inventory
            InventoryResult msg;
            player->StoreLootItem(i, loot, msg);

            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalItemsLooted++;
                itemsLootedFromCorpse++;

                LOG_DEBUG("module.aoe_loot", "AOE Loot: Successfully looted quest item (ID: {}) x{} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Successfully looted quest item (ID: {}) x{} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetGUID().ToString()));
            }
        }

        // Process items - second pass for non-quest items
        for (uint32 i = 0; i < maxSlots; ++i)
        {
            LootItem* lootItem = loot->LootItemInSlot(i, player);

            if (!lootItem)
                continue;
                
            // Check if player can loot this item
            if (!lootItem->AllowedForPlayer(player, creature->GetGUID()))
            {
                continue;
            }
            
            // Skip items that are currently being rolled on in group
            if (lootItem->is_blocked)
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    totalSkippedItems++;

                    LOG_DEBUG("module.aoe_loot", "AOE Loot: Item in slot {} is currently being rolled on - skipping", i);
                    handler->PSendSysMessage(fmt::format("AOE Loot: Item in slot {} is currently being rolled on - skipping", i));
                }
                continue;
            }
                
            // Second pass - skip quest items (already processed)
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(lootItem->itemid);

            if (!itemTemplate || (itemTemplate->Class == ITEM_CLASS_QUEST))
                continue;
                
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Attempting to loot item ID: {} Count: {} Slot: {} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Attempting to loot item ID: {} Count: {} Slot: {} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetGUID().ToString()));
            }

            InventoryResult msg;
            player->StoreLootItem(i, loot, msg);

            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalItemsLooted++;
                itemsLootedFromCorpse++;

                LOG_DEBUG("module.aoe_loot", "AOE Loot: Successfully looted item (ID: {}) x{} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Successfully looted item (ID: {}) x{} from {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetGUID().ToString()));
            }
        }

        // Process gold
        if (loot->gold > 0)
        {
            uint32 copperAmount = loot->gold;
            player->ModifyMoney(copperAmount);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, copperAmount);
            loot->gold = 0;
        
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalCopperLooted += copperAmount;

                LOG_DEBUG("module.aoe_loot", "AOE Loot: Looted {} copper from {}", copperAmount, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} copper from {}", copperAmount, creature->GetGUID().ToString()));
            }
            handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} copper from {}", copperAmount, creature->GetGUID().ToString()));
        }

        // Check if the corpse is now fully looted
        if (loot->isLooted())
        {
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                totalCorpsesLooted++;
                LOG_DEBUG("module.aoe_loot", "AOE Loot: All loot removed from corpse for {}", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: All loot removed from corpse for {}", creature->GetGUID().ToString()));
            }
            
            // Cleanup the loot object from the corpse
            creature->AllLootRemovedFromCorpse();
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }
    }

    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Summary - Looted {} items, {} gold from {} corpses, skipped {} items", totalItemsLooted, totalCopperLooted, totalCorpsesLooted, totalSkippedItems);
        handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} items and {} gold from {} corpses", totalItemsLooted, totalCopperLooted, totalCorpsesLooted)); 
    }

    return true;
}

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
