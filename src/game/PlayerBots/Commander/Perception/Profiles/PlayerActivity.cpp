#include "Commander/Perception/Profiles/PlayerActivity.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "SpellAuraDefines.h"

namespace Commander
{
namespace PlayerActivities
{
    namespace
    {
        // The NPC he deals with: selected, friendly, with something to offer, in reach.
        ObjectGuid NpcAt(Player const* player)
        {
            ObjectGuid const selection = player->GetSelectionGuid();
            if (!selection.IsCreature() || player->IsMoving())
                return ObjectGuid();
            Creature* npc = player->GetMap()->GetCreature(selection);
            if (!npc || !npc->IsAlive() || !npc->GetUInt32Value(UNIT_NPC_FLAGS) || npc->IsHostileTo(player) ||
                !player->IsWithinDistInMap(npc, INTERACTION_DISTANCE + NPC_SLACK_YD))
                return ObjectGuid();
            return selection;
        }
    }

    PlayerActivity Read(Player const* player)
    {
        PlayerActivity a;
        if (!player || !player->IsInWorld())
            return a;
        a.dead = !player->IsAlive();
        a.ghost = player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
        a.afk = player->IsAFK();
        a.mounted = player->IsMounted();
        a.taxi = player->IsTaxiFlying();
        a.moving = player->IsMoving();
        if (a.dead)
            return a;
        a.looting = !player->GetLootGuid().IsEmpty();
        a.trading = player->GetTradeData() != nullptr;
        a.sitting = player->IsSittingDown();
        a.eating = a.sitting && player->HasAuraType(SPELL_AURA_MOD_REGEN);
        a.drinking = a.sitting && player->HasAuraType(SPELL_AURA_MOD_POWER_REGEN);
        a.casting = !player->IsInCombat() && player->IsNonMeleeSpellCasted(false);
        if (!player->IsInCombat())
            a.atNpc = NpcAt(player);
        return a;
    }
}
}
