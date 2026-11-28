/*
 * PartyBot Commander boss module: Ambassador Flamelash, Blackrock Depths (boss doc 2.1, P1).
 *
 * Script (instance_blackrock_depths.cpp, creature_ai_events 9156): while he fights, each of
 * the 7 dwarf runes in the room sends a Burning Spirit (9178) every 15-30 s, running to him
 * (MoveFollow). Every 5 s he takes the spirits within 4 yd of him (script 915604), each one a
 * stack of Burning Spirit (14744: +damage done, up to 50 stacks, 3 min). The spirits are
 * neutral (faction 7) and fight nobody, so they are on no threat list.
 *
 * Match: map 230 and Flamelash (9156) among the enemies.
 *
 * Eyes (ListEnemies): the spirits within SPIRIT_SEARCH_YD of him, as listed enemies
 *   (WorldModel::targeted, EnemyFacts::listed), so they can be named below.
 * Duties (OfferDuties): one kill need per spirit (EncounterHelpers::AddKillNeed), now; points =
 *   the stack's extra damage on his victim (STACK_DAMAGE per swing) over the rest of the fight
 *   (his health / the party's damage), at least MIN_FIGHT_SEC. Never the one he hits (the tank
 *   stays on him); the spirit closest to him first on equal points.
 * Voice: one line when the first spirit shows up.
 *
 * Known limits:
 *   - The stacks he already has are not read (no "kill him fast, he is at 20 stacks").
 *   - The spirits' own health is not weighed against the stack (they die in a few hits).
 *   - How 13489 (the spirit's self cast) reaches his "hit by spell" event is not checked;
 *     the effect is taken as "within 4 yd every 5 s: one stack each".
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_FLAMELASH_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_FLAMELASH_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"

namespace Commander
{
    class BlackrockDepthsFlamelash : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        static constexpr uint32 NPC_FLAMELASH = 9156;
        static constexpr uint32 NPC_BURNING_SPIRIT = 9178;

        // The runes stand around his room; spirits farther than this are not his.
        static constexpr float SPIRIT_SEARCH_YD = 70.0f;
        // Spell 14744 (5302): +30 damage done per stack.
        static constexpr float STACK_DAMAGE = 30.0f;
        // The stack lasts the fight; a fight about to end still counts this long.
        static constexpr float MIN_FIGHT_SEC = 10.0f;

        char const* GetId() const override { return "flamelash"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ListEnemies(TeamBotAI* bot, WorldModel const& world, std::vector<Creature*>& out) const override;
        void OfferDuties(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& needs) const override;
        void OnReset() override;

    private:
        ObjectGuid m_boss;
        std::vector<ObjectGuid> m_spirits;      // alive spirits around him, this refresh (Refresh)
        bool m_spiritsNoted = false;
    };
}

#endif
