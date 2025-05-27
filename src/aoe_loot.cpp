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

extern std::map<uint64, bool> playerAoeLootEnabled;

bool AoeLootServer::CanPacketReceive(WorldSession* session, WorldPacket& packet)
{
    if (packet.GetOpcode() == CMSG_LOOT)
    {
        Player* player = session->GetPlayer();
        if (player)
        {
            uint64 guid = player->GetGUID().GetRawValue();
            
            // Check if player has explicitly disabled AOE loot
            if (playerAoeLootEnabled.find(guid) != playerAoeLootEnabled.end() && 
                !playerAoeLootEnabled[guid])
            {
                // Let normal looting proceed
                return true;
            }
            // Trigger AOE loot when a player attempts to loot a corpse
            ChatHandler handler(player->GetSession());
            handler.ParseCommands(".aoeloot startaoeloot");
        }
    }
    return true;
}

ChatCommandTable AoeLootCommandScript::GetCommands() const
{
    static ChatCommandTable aoeLootSubCommandTable =
    {
        { "startaoeloot", HandleStartAoeLootCommand, SEC_PLAYER, Console::No },
        { "on",  HandleAoeLootOnCommand,  SEC_PLAYER, Console::No },
        { "off", HandleAoeLootOffCommand, SEC_PLAYER, Console::No }
    };

    static ChatCommandTable aoeLootCommandTable =
    {
        { "aoeloot", nullptr, SEC_PLAYER, Console::No, aoeLootSubCommandTable }
    };
    
    return aoeLootCommandTable;
}

bool AoeLootCommandScript::HandleAoeLootOnCommand(ChatHandler* handler, Optional<std::string> /*args*/)
{
    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
        return true;

    uint64 guid = player->GetGUID().GetRawValue();
    playerAoeLootEnabled[guid] = true;

    handler->PSendSysMessage("AOE looting has been enabled for your character. Type: '.aoeloot off' to turn AoE Looting Off.");
    return true;
}

bool AoeLootCommandScript::HandleAoeLootOffCommand(ChatHandler* handler, Optional<std::string> /*args*/)
{
    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
        return true;

    uint64 guid = player->GetGUID().GetRawValue();
    playerAoeLootEnabled[guid] = false;

    handler->PSendSysMessage("AOE looting has been disabled for your character. Type: '.aoeloot on' to turn AoE Looting on.");
    return true;
}

void AoeLootCommandScript::ProcessLootRelease(ObjectGuid lguid, Player* player, Loot* loot)
{
    player->SetLootGUID(ObjectGuid::Empty);
    player->SendLootRelease(lguid);
    player->RemoveUnitFlag(UNIT_FLAG_LOOTING);

    if (!player->IsInWorld())
        return;

    if (lguid.IsGameObject())
    {
        GameObject* go = player->GetMap()->GetGameObject(lguid);

        // not check distance for GO in case owned GO (fishing bobber case, for example) or Fishing hole GO
        if (!go || ((go->GetOwnerGUID() != player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(player)))
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &go->loot;

    }
    else if (lguid.IsCorpse()) // ONLY remove insignia at BG
    {
        Corpse* corpse = ObjectAccessor::GetCorpse(*player, lguid);
        
        if (!corpse)
            return;

        loot = &corpse->loot;
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);
        if (!pItem)
            return;

    }
    else // Must be a creature
    {
        Creature* creature = player->GetMap()->GetCreature(lguid);

        // Skip distance check for dead creatures (corpses)
        // Keep distance check for pickpocketing (live creatures)
        bool isPickpocketing = creature && creature->IsAlive() && player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING;
        
        // Only check distance for pickpocketing, not for corpse looting
        if (isPickpocketing && !creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
        {
            player->SendLootError(lguid, LOOT_ERROR_TOO_FAR);
            return;
        }
        
        loot = &creature->loot;
        
        
        if (loot->isLooted())
        {
            // skip pickpocketing loot for speed, skinning timer reduction is no-op in fact
            if (!creature->IsAlive())
                creature->AllLootRemovedFromCorpse();

            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            loot->clear();
        }
        else
        {
            // if the round robin player release, reset it.
            if (player->GetGUID() == loot->roundRobinPlayer)
            {
                loot->roundRobinPlayer.Clear();

                if (Group* group = player->GetGroup())
                    group->SendLooter(creature, nullptr);
            }
            // force dynflag update to update looter and lootable info
            creature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
        }
    }

    // Player is not looking at loot list, he doesn't need to see updates on the loot list
    if (!lguid.IsItem())
    {
        loot->RemoveLooter(player->GetGUID());
    }
}

// Handle gold looting without distance checks
bool AoeLootCommandScript::ProcessLootMoney(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
        
    Loot* loot = &creature->loot;
    if (!loot || loot->gold == 0)
        return false;
        
    uint32 goldAmount = loot->gold;
    bool shareMoney = true;  // Share by default for creature corpses
    
    if (shareMoney && player->GetGroup())
    {
        Group* group = player->GetGroup();
        std::vector<Player*> playersNear;
        
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (member)
                playersNear.push_back(member);
        }
        
        uint32 goldPerPlayer = uint32((loot->gold) / (playersNear.size()));
        
        for (Player* groupMember : playersNear)
        {
            groupMember->ModifyMoney(goldPerPlayer);
            groupMember->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);
            
            WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
            data << uint32(goldPerPlayer);
            data << uint8(playersNear.size() > 1 ? 0 : 1);  // 0 is "Your share is..." and 1 is "You loot..."
            groupMember->GetSession()->SendPacket(&data);
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
    
    return true;
}

bool AoeLootCommandScript::ProcessLootSlot(Player* player, ObjectGuid lguid, uint8 lootSlot)
{
    if (!player)
        return false;

    Loot* loot = nullptr;
    float aoeDistance = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0f);

    // Get the loot object based on the GUID type
    if (lguid.IsGameObject())
    {
        GameObject* go = player->GetMap()->GetGameObject(lguid);
        
        if (!go || ((go->GetOwnerGUID() != player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(player, aoeDistance)))
        {
            player->SendLootRelease(lguid);
            return false;
        }
        
        loot = &go->loot;
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);
        if (!pItem)
        {

            player->SendLootRelease(lguid);
            return false;
        }
        
        loot = &pItem->loot;
    }
    else if (lguid.IsCorpse())
    {
        Corpse* bones = ObjectAccessor::GetCorpse(*player, lguid);
        if (!bones)
        {
            player->SendLootRelease(lguid); 
            return false;
        }
        loot = &bones->loot;
    }
    else
    {
        Creature* creature = player->GetMap()->GetCreature(lguid);

        // Skip distance check for dead creatures (corpses)
        // Keep distance check for pickpocketing (live creatures)
        bool isPickpocketing = creature && creature->IsAlive() && player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING;
        
        // Only check distance for pickpocketing, not for corpse looting
        if (isPickpocketing && !creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
        {
            player->SendLootError(lguid, LOOT_ERROR_TOO_FAR);
            return false;
        }
        
        loot = &creature->loot;

    }

    sScriptMgr->OnPlayerAfterCreatureLoot(player);
    if (!loot)
        return false;

    InventoryResult msg;
    LootItem* lootItem = player->StoreLootItem(lootSlot, loot, msg);
    if (msg != EQUIP_ERR_OK && lguid.IsItem() && loot->loot_type != LOOT_CORPSE)
    {
        lootItem->is_looted = true;
        loot->NotifyItemRemoved(lootItem->itemIndex);
        loot->unlootedCount--;

        player->SendItemRetrievalMail(lootItem->itemid, lootItem->count);
    }

    // If player is removing the last LootItem, delete the empty container
    if (loot->isLooted() && lguid.IsItem())
    {
        ProcessLootRelease(lguid, player, loot);   
    }
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
    bool debugMode = sConfigMgr->GetOption<bool>("AOELoot.Debug", false);

    std::list<Creature*> nearbyCorpses;
    player->GetDeadCreatureListInGrid(nearbyCorpses, range);

    if (debugMode)
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} nearby corpses within range {}.", nearbyCorpses.size(), range);
    }

    // Filter valid corpses
    std::list<Creature*> validCorpses;
    for (auto* creature : nearbyCorpses)
    {
        if (!player || !creature)
            continue;

        // Check if creature is valid for looting by this player
        if (!player->isAllowedToLoot(creature))
        {
            if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - not your loot", creature->GetGUID().ToString());
            continue;
        }

        if (!creature->hasLootRecipient() || !creature->isTappedBy(player))
            continue;

        // Get player's group and check loot permissions based on group loot method
        Group* group = player->GetGroup();
        if (group)
        {
            Loot* loot = &creature->loot;
            LootMethod lootMethod = group->GetLootMethod();
            // For Round Robin loot, check if this player is the designated looter
            if (lootMethod == ROUND_ROBIN)
            {
                if (loot->roundRobinPlayer && loot->roundRobinPlayer != player->GetGUID())
                {
                    if (debugMode)
                        LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - not your turn (Round Robin)", creature->GetGUID().ToString());
                    
                    continue;
                }
            }
            // For Master Loot, check if this player is the master looter
            else if (lootMethod == MASTER_LOOT)
            {
                if (group->GetMasterLooterGuid() != player->GetGUID())
                {
                    if (debugMode)
                        LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - not master looter", creature->GetGUID().ToString());

                    continue;
                }
            }
        }

        // Skip if corpse is not lootable
        if (!creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
        {
            if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Skipping creature {} - not lootable", creature->GetGUID().ToString());
            continue;
        }
        
        validCorpses.push_back(creature);
    }

    if (debugMode)
    {
        LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} valid nearby corpses within range {}.", validCorpses.size(), range);
    }

    // Process all valid corpses
    for (auto* creature : validCorpses)
    {
        ObjectGuid lguid = creature->GetGUID();
        Loot* loot = &creature->loot;
        if (validCorpses.size() <= 1)
        {
            break;
        }
        if (!loot)
            continue;

        player->SetLootGUID(lguid);
       
        // Process quest items
        QuestItemMap const& playerNonQuestNonFFAConditionalItems = loot->GetPlayerNonQuestNonFFAConditionalItems();
        if (!playerNonQuestNonFFAConditionalItems.empty())
        {
            for (uint8 i = 0; i < playerNonQuestNonFFAConditionalItems.size(); ++i)
            {
                uint8 lootSlot = playerNonQuestNonFFAConditionalItems.size() + i;
                ProcessLootSlot(player, lguid, lootSlot);
                if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted quest item in slot {}", lootSlot);
            }
        }

        // Process quest items
        QuestItemMap const& playerFFAItems = loot->GetPlayerFFAItems();
        if (!playerFFAItems.empty())
        {
            for (uint8 i = 0; i < playerFFAItems.size(); ++i)
            {
                uint8 lootSlot = playerFFAItems.size() + i;
                ProcessLootSlot(player, lguid, lootSlot);
                if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted quest item in slot {}", lootSlot);
            }
        }

        // Process quest items
        QuestItemMap const& playerQuestItems = loot->GetPlayerQuestItems();
        if (!playerQuestItems.empty())
        {
            for (uint8 i = 0; i < playerQuestItems.size(); ++i)
            {
                uint8 lootSlot = playerQuestItems.size() + i;
                ProcessLootSlot(player, lguid, lootSlot);
                if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted quest item in slot {}", lootSlot);
            }
        }

        // Process quest items
        if (!loot->quest_items.empty())
        {
            for (uint8 i = 0; i < loot->quest_items.size(); ++i)
            {
                uint8 lootSlot = loot->items.size() + i;
                ProcessLootSlot(player, lguid, lootSlot);
                if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted quest item in slot {}", lootSlot);
            }
        }
 
        // Process quest items
        if (!loot->quest_items.empty())
        {
            for (uint8 i = 0; i < loot->quest_items.size(); ++i)
            {
                uint8 lootSlot = loot->items.size() + i;
                ProcessLootSlot(player, lguid, lootSlot);
                if (debugMode)
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted quest item in slot {}", lootSlot);
            }
        }
    
        // Process regular items
        for (uint8 lootSlot = 0; lootSlot < loot->items.size(); ++lootSlot)
        {
            player->SetLootGUID(lguid);
            ProcessLootSlot(player, lguid, lootSlot);
            if (debugMode && lootSlot < loot->items.size())
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: looted item from slot {} (ID: {}) from {}", lootSlot, loot->items[lootSlot].itemid, lguid.ToString());
            }
        }

        // Handle money
        if (loot->gold > 0)
        {
            uint32 goldAmount = loot->gold;
            ProcessLootMoney(player, creature);
            if (debugMode)
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Looted {} copper from {}", goldAmount, lguid.ToString());
            }
        }
        
        if (loot->isLooted())
        {
            ProcessLootRelease(lguid, player, loot); 
        }
    }
    return true;
}

// Display login message to player
void AoeLootPlayer::OnPlayerLogin(Player* player)
{
    if (sConfigMgr->GetOption<bool>("AOELoot.Enable", true) && 
        sConfigMgr->GetOption<bool>("AOELoot.Message", true))
    {
        ChatHandler(player->GetSession()).PSendSysMessage(AOE_ACORE_STRING_MESSAGE);
    }
}

// Add script registrations
void AddSC_AoeLoot()
{
    new AoeLootPlayer();
    new AoeLootServer();
    new AoeLootCommandScript();
}
