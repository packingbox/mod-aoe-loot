#include "aoe_loot.h"
#include "LootMgr.h"
#include "WorldSession.h" // Include for HandleAutostoreLootItemOpcode
#include "WorldPacket.h" // Include for WorldPacket
#include "Player.h"
#include "Chat.h"
#include "Creature.h"


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
    LOG_DEBUG("module.aoe_loot", "CanPacketReceive function called for opcode {}", packet.GetOpcode());
    if (packet.GetOpcode() == CMSG_LOOT)
    {
        Player* player = session->GetPlayer();

        if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
            return true;

        if (player->GetGroup() && !sConfigMgr->GetOption<bool>("AOELoot.Group", true))
            return true;

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


            // Process quest items
            for (size_t i = 0; i < loot->quest_items.size(); ++i)
            {
                player->SetLootGUID(creature->GetGUID());
                WorldPacket autostorePacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                autostorePacket << i; // Use the index i as the slot for this creature
                session->HandleAutostoreLootItemOpcode(autostorePacket);
            }


            // Process regular items
            for (size_t i = 0; i < loot->items.size(); ++i)
            {
                player->SetLootGUID(creature->GetGUID());
                WorldPacket autostorePacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                autostorePacket << i; // Use the index i as the slot for this creature
                session->HandleAutostoreLootItemOpcode(autostorePacket);
            }


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
