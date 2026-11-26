/*
 * PartyBot Commander layer 2: which totem each shaman bot keeps up per element ("totems"
 * assigner, user 2026-11-15). Until then every shaman picked one random totem per element at
 * spell load (PopulateSpellData), Earthbind, Stoneclaw, Fire Nova, Magma and Searing among them:
 * they hit or pull enemies and broke the party's controls.
 *
 * Per element, the list Order() gives for the party (players, not pets):
 *   water  Mana Spring when someone uses mana, then Healing Stream
 *   earth  Strength of Earth with melee (tank or melee damage), else Stoneskin first
 *   air    Windfury, then Grace of Air with melee; without melee Tranquil Air
 *   fire   Flametongue with melee; else none
 * Each shaman bot (guid order) takes the first kind of each list it knows that no shaman bot
 * before it took (the same totem twice does not stack). Totems of player shamans are not seen.
 *
 * Not a Duties module, like the blessings: a shaman keeps four totems at once. Runs every
 * refresh (pure function of the world model); ShamanRotation drops them in combat (no totems
 * without this plan since 2026-11-26).
 */

#ifndef MANGOS_COMMANDER_TOTEM_ASSIGNER_H
#define MANGOS_COMMANDER_TOTEM_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Perception/Profiles/ShamanTotems.h"
#include <vector>

namespace Commander
{
    class TotemAssigner : public IAssigner
    {
    public:
        // What the party is made of, for Order().
        struct PartyMix
        {
            uint32 melee = 0;       // tanks and melee damage dealers
            uint32 manaUsers = 0;
        };

        // The element's totems, most wanted first (header).
        static std::vector<TotemKind> Order(TotemSlot slot, PartyMix const& mix);

        char const* GetId() const override { return "totems"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
