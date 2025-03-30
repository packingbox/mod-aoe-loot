#include "aoe_loot.h"
#include "LootMgr.h"
#include "WorldSession.h" // Include for HandleAutostoreLootItemOpcode
#include "WorldPacket.h" // Include for WorldPacket
#include "Player.h"
#include "Chat.h"
#include "Creature.h"
#include "Config.h"


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
        if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
            return true;

        if (player->GetGroup() && !sConfigMgr->GetOption<bool>("AOELoot.Group", true))
            return true;
        
        Player* player = session->GetPlayer();

        float range = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0);

        std::list<Creature*> lootcreature;
        player->GetDeadCreatureListInGrid(lootcreature, range);

        ObjectGuid mainGuid;
        packet >> mainGuid;

        for (auto* creature : lootcreature)
        {

            if (!player->GetMap()->Instanceable() && !player->isAllowedToLoot(creature))
                continue;

            Loot* loot = &creature->loot;
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true)){
                LOG_DEBUG("module.aoe_loot", "Quest Items Size for {}: {}", creature->GetAIName(), loot->quest_items.size());
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Quest Items Size for {}: {}", creature->GetAIName(), loot->quest_items.size()));
            }
            // Process quest items
            for (size_t i = 0; i < loot->quest_items.size(); ++i)
            {
                player->SetLootGUID(creature->GetGUID());
                WorldPacket autostorePacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                autostorePacket << i; // Use the index i as the slot for this creature
                session->HandleAutostoreLootItemOpcode(autostorePacket);
            }
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true)){
                LOG_DEBUG("module.aoe_loot", "Regular Items Size for {}: {}", creature->GetAIName(), loot->items.size());
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Regular Items Size for {}: {}", creature->GetAIName(), loot->items.size()));
            }
            // Process regular items
            for (size_t i = 0; i < loot->items.size(); ++i)
            {
                player->SetLootGUID(creature->GetGUID());
                WorldPacket autostorePacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                autostorePacket << i; // Use the index i as the slot for this creature
                session->HandleAutostoreLootItemOpcode(autostorePacket);
            }
            if (sConfigMgr->GetOption<bool>("AOELoot.Debug", true)){
                LOG_DEBUG("module.aoe_loot", "Gold Amount for {}: {}", creature->GetAIName(), loot->gold);
                ChatHandler(player->GetSession()).PSendSysMessage(fmt::format("AOE Loot: Found {} gold on nearby corpse.", loot->gold));
            }
            // Process gold
            if (creature->loot.gold > 0)
            {
                player->SetLootGUID(creature->GetGUID());
                WorldPacket moneyPacket(CMSG_LOOT_MONEY, 0); // Create a dummy packet
                session->HandleLootMoneyOpcode(moneyPacket);
            }

            if (!loot->empty())
            {
                creature->loot.clear();
                creature->AllLootRemovedFromCorpse();
                creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            }
        }
        return true;
    }
    return true;
}
