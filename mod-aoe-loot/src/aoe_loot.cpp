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
using namespace WorldPackets;

bool AoeLootServer::CanPacketReceive(WorldSession* session, WorldPacket& packet)
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

// Custom function to handle gold looting without distance checks
bool AoeLootCommandScript::ProcessLootMoney(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
        
    Loot* loot = &creature->loot;
    if (!loot || loot->gold == 0)
        return false;
        
    // Store gold amount before we modify it
    uint32 goldAmount = loot->gold;
    
    // Handle group money distribution if needed
    bool shareMoney = true;  // Share by default for creature corpses
    
    if (shareMoney && player->GetGroup())
    {
        Group* group = player->GetGroup();
        
        std::vector<Player*> playersNear;
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;
                
            // For AOE looting, we don't do a distance check - just include all group members
            playersNear.push_back(member);
        }
        
        uint32 goldPerPlayer = uint32((loot->gold) / (playersNear.size()));
        
        for (std::vector<Player*>::const_iterator i = playersNear.begin(); i != playersNear.end(); ++i)
        {
            (*i)->ModifyMoney(goldPerPlayer);
            (*i)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);
            
            WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
            data << uint32(goldPerPlayer);
            data << uint8(playersNear.size() > 1 ? 0 : 1);  // 0 is "Your share is..." and 1 is "You loot..."
            (*i)->GetSession()->SendPacket(&data);
        }
    }
    else
    {
        // No group - give all gold to the player
        player->ModifyMoney(loot->gold);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, loot->gold);
        
        WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
        data << uint32(loot->gold);
        data << uint8(1);   // "You loot..."
        player->GetSession()->SendPacket(&data);
    }
    
    // Mark the money as looted
    loot->gold = 0;
    loot->NotifyMoneyRemoved();
    
    // Delete container if empty
    if (loot->isLooted())
    {
        // The loot release will be handled in the main function
    }
    
    return true;
}

bool AoeLootCommandScript::ProcessLootSlot(Player* player, ObjectGuid lguid, uint8 lootSlot)
{
    if (!player)
        return false;

    Loot* loot = nullptr;

    // Get the loot object based on the GUID type.
    if (lguid.IsGameObject())
    {
        GameObject* go = player->GetMap()->GetGameObject(lguid);
        
        // Use increased distance for AOE looting
        float aoeDistance = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0f);
        
        // Modified distance check
        if (!go || ((go->GetOwnerGUID() != player->GetGUID() && 
            go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && 
            !go->IsWithinDistInMap(player, aoeDistance)))
        {
            // We're outside of range - silently fail when batch processing
            return false;
        }
        
        loot = &go->loot;
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);
        if (!pItem)
            return false;
        
        loot = &pItem->loot;
    }
    else if (lguid.IsCorpse())
    {
        Corpse* bones = ObjectAccessor::GetCorpse(*player, lguid);
        if (!bones)
            return false;
        
        loot = &bones->loot;
    }
    else
    {
        Creature* creature = player->GetMap()->GetCreature(lguid);
        
        if (creature && creature->IsAlive())
        {
            bool isPickpocketing = creature && creature->IsAlive() && (player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING);
            // Use regular interaction distance for pickpocketing
            bool lootAllowed = creature && creature->IsAlive() == isPickpocketing;
            // Modified distance check with appropriate range
            if (!lootAllowed || !creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
            {
                // We're outside of range - silently fail when batch processing
                return false;
            }
        }
        loot = &creature->loot;
    }
    
    if (!loot)
        return false;
    
    // Check if the item is already looted
    QuestItem* qitem = nullptr;
    QuestItem* ffaitem = nullptr;
    QuestItem* conditem = nullptr;
    LootItem* item = loot->LootItemInSlot(lootSlot, player, &qitem, &ffaitem, &conditem);
    if (!item)
    {
        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
        {
            LOG_DEBUG("module.aoe_loot", "No valid loot item found in slot {}", lootSlot);
        }
        return false;
    }
    
    // Now call StoreLootItem with the validated slot
    InventoryResult msg;
    
    // Use StoreLootItem directly
    player->StoreLootItem(lootSlot, loot, msg);
    if (msg == EQUIP_ERR_OK )
    {
        // Successfully looted the item
        loot->items[lootSlot].is_looted = true;
        loot->unlootedCount--;
        
        // Notify the player about the looted item
        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
        {
            LOG_DEBUG("module.aoe_loot", "Successfully looted item (ID: {}) x{} from {}", item->itemid, static_cast<uint32_t>(item->count), lguid.ToString());
        }
    }
    // Handle mail fallback for items
    if (msg != EQUIP_ERR_OK && lguid.IsItem() && loot->loot_type != LOOT_CORPSE)
    {
        item->is_looted = true;
        loot->NotifyItemRemoved(item->itemIndex);
        loot->unlootedCount--;
        
        player->SendItemRetrievalMail(item->itemid, item->count);
    }
    
    // Success
    return true;
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

    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range);
        handler->PSendSysMessage(fmt::format("AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range));
    }

    // Filter valid corpses first
    std::list<Creature*> validCorpses;
    // Process each corpse one by one
    for (auto* creature : nearbyCorpses)
    {
        if (!player || !creature)
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
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - ***NOT LOOTABLE***", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - ***NOT LOOTABLE***", creature->GetGUID().ToString()));
            }
            continue;
        }

        // Additional permission check
        if (player->GetMap()->Instanceable() && !player->isAllowedToLoot(creature))
        {
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - ***NOT YOUR LOOT***", creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: Skipping creature {} - ***NOT YOUR LOOT***", creature->GetGUID().ToString()));
            }
            continue;
        }
        // Add to valid list of creatures to loot
        validCorpses.push_back(creature);

    }
    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} VALID nearby corpses within range {}.", validCorpses.size(), range);
        handler->PSendSysMessage(fmt::format("AOE Loot: Found {} VALID nearby corpses within range {}.", validCorpses.size(), range));
    }
    // Process all corpses silently and quickly
    for (auto* creature : validCorpses)
    {
        Loot* loot = &creature->loot;

        if (!loot)
        {
            continue;
        }

        // Process quest items
        if (!loot->quest_items.empty())
        {
            // Try to loot each potential quest slot
            for (uint8 i = 0; i < loot->quest_items.size(); ++i)
            {
                uint8 lootSlot = loot->items.size() + i;

                // Store the loot item in the player's inventory
                AoeLootCommandScript::ProcessLootSlot(player, creature->GetGUID(), lootSlot);
                // Debug logging
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {   
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: looted QUEST ITEM in slot {}", lootSlot);
                    handler->PSendSysMessage(fmt::format("AOE Loot: looted QUEST ITEM in slot {}", lootSlot)); 
                }
            }
        }
    
        // Process regular items
        for (uint8 lootSlot = 0; lootSlot < loot->items.size(); ++lootSlot)
        {
            player->SetLootGUID(creature->GetGUID());
            // Store the loot item in the player's inventory
            AoeLootCommandScript::ProcessLootSlot(player, creature->GetGUID(), lootSlot);

            // Debug
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted item from slot {} (ID: {}) from {}", lootSlot, loot->items[lootSlot].itemid, creature->GetGUID().ToString());
                handler->PSendSysMessage(fmt::format("AOE Loot: looted item from slot {} (ID: {})", lootSlot, loot->items[lootSlot].itemid));
            }
        }

        // Handle money with direct packet
        if (loot->gold > 0)
        {
            // Store the gold amount before it gets cleared by HandleLootMoneyOpcode
            uint32 goldAmount = loot->gold;
            AoeLootCommandScript::ProcessLootMoney(player, creature);
            

             // Debug
             if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
             {
                 LOG_DEBUG("module.aoe_loot", "AOE Loot: Looted {} copper from {}", goldAmount, creature->GetGUID().ToString());
                 handler->PSendSysMessage(fmt::format("AOE Loot: Looted {} copper from {}", goldAmount, creature->GetGUID().ToString()));
             }  
        }

        // Check if the corpse is now fully looted
        if (loot->isLooted())
        {
            WorldPacket releaseData(CMSG_LOOT_RELEASE, 8);
            releaseData << creature->GetGUID();
            player->GetSession()->HandleLootReleaseOpcode(releaseData);
            // Cleanup the loot object from the corpse
            //creature->AllLootRemovedFromCorpse();
            //creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            // Force update the world state to reflect the loot being gone
        }
    }
    return true;
}

// Copied from sudlud's "mod-aoe-loot" module.
void AoeLootPlayer::OnPlayerLogin(Player* player)
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
    new AoeLootPlayer();
    new AoeLootServer();
    new AoeLootCommandScript();
}
