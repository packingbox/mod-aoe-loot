#include "aoe_loot.h"
#include "ScriptMgr.h" 
#include "LootMgr.h"
#include "WorldSession.h" // Include for HandleAutostoreLootItemOpcode
#include "WorldPacket.h" 
#include "Player.h"
#include "PlayerScript.h" 
#include "Chat.h"
#include "WorldObjectScript.h"
#include "Creature.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"


void AOELootPlayer::OnPlayerLogin(Player* player)
{
    if (sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
    {
        if (sConfigMgr->GetOption<bool>("AOELoot.Message", true))
            ChatHandler(player->GetSession()).PSendSysMessage(AOE_ACORE_STRING_MESSAGE);
    }
}

bool AOELootServer::CanPacketReceive(WorldSession* session, WorldPacket& packet)
{
    if (packet.GetOpcode() == CMSG_LOOT)
    {
        // Check if AoE Loot is enabled in the config
        if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
            return false;
        
        // Grab the player from the session
        Player* player = session->GetPlayer();

        // Set AoE Loot range from config
        float range = sConfigMgr->GetOption<float>("AOELoot.Range", 120.0);

        // Create a list of creature corpses within the specified range
        std::list<Creature*> lootcreature;
        player->GetDeadCreatureListInGrid(lootcreature, range);

        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
        {
            LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} nearby corpses within range.", lootcreature.size());
            ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Found {} nearby corpses within range.", lootcreature.size()));
        }
        // Cycle through the list of corpses
        for (auto* creature : lootcreature)
        {
            // Check if the creature is valid.
            if (!creature)
                continue;

            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {   
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Processing creature with {}", creature->GetGUID().ToString());
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Processing creature with {}", creature->GetGUID().ToString()));
            }

            // Check if the player is allowed to loot the creature
            if (!player->GetMap()->Instanceable() && !player->isAllowedToLoot(creature))
                continue;

            player->SetLootGUID(creature->GetGUID());
            
            // Get the loot object from the creature
            Loot* loot = &creature->loot;

            // Get the max slots of loot from creature. Uses: hasLootFor(Player* player).
            uint32 maxSlots = loot->GetMaxSlotInLootFor(player);

            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "Max Slots for {}: {}", creature->GetAIName(), maxSlots);
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Max Slots for {}: {}", creature->GetAIName(), maxSlots));
            }
            // Cycle through the loot slots       
            for (uint32 i = 0; i < maxSlots; ++i)
            {
                // Create a packet to autostore the loot item
                WorldPacket autostorePacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                // Set the slot index in the packet
                autostorePacket << i;
                // Send the packet to autostore the loot item in the specified slot
                LootItem* lootItem = loot->LootItemInSlot(i, player);
                if (lootItem)
                {
                    if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                    {
                        LOG_DEBUG("module.aoe_loot", "Loot Item ID: {} Count: {} Slot: {} Creature: {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetAIName());
                        ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: (Slot {}): Found item (ID: {}) Count: {} on {}", i, lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetAIName()));
                    }
                    if (lootItem->needs_quest) // Check if it's a quest item
                    {
                        if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                        {
                            LOG_DEBUG("module.aoe_loot", "Loot Item ID: {} Count: {} Slot: {} Creature: {}", lootItem->itemid, static_cast<uint32_t>(lootItem->count), i, creature->GetAIName());
                            ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot (Slot {} - Quest): Attempting to loot item (ID: {}) Count: {} on {}", i, lootItem->itemid, static_cast<uint32_t>(lootItem->count), creature->GetAIName()));
                        }
                    }
                    // Send the loot packet info to the session opcode handler to be given to the player character
                    session->HandleAutostoreLootItemOpcode(autostorePacket);
                }
            }
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Gold Amount for {}: {}", creature->GetAIName(), loot->gold);
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Found {} gold on {}", loot->gold, creature->GetAIName()));
            }

            // Process gold
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
            {
                LOG_DEBUG("module.aoe_loot", "AOE Loot: Found {} gold on creature with {}", loot->gold, creature->GetAIName());
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Found {} gold on creature with {}", loot->gold, creature->GetAIName()));
            }
            if (creature->loot.gold > 0)
            {
                player->SetLootGUID(creature->GetGUID());
                // Create a dummy packet for money loot
                WorldPacket moneyPacket(CMSG_LOOT_MONEY, 0);
                // Send the packet to loot money
                session->HandleLootMoneyOpcode(moneyPacket);
            }

            // Check if the loot is empty
            if (loot->empty())
            {
                if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true))
                {
                    LOG_DEBUG("module.aoe_loot", "AOE Loot: All loot removed from corpse for {}", creature->GetAIName());
                    ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: All loot removed from corpse for {}", creature->GetAIName()));
                }
                // Cleanup the loot object from the corpse
                creature->AllLootRemovedFromCorpse();
                // Remove the lootable flag from the creature
                creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
                creature->RemoveFromWorld();
            }
        }
        return true;
    }
    return true;
}
