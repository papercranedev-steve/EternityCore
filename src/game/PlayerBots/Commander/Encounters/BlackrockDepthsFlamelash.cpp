/*
 * Ambassador Flamelash boss module (header comment: script, eyes, duties, limits).
 */

#include "Commander/Encounters/BlackrockDepthsFlamelash.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <list>

namespace Commander
{
    bool BlackrockDepthsFlamelash::Matches(WorldModel const& world, uint32 mapId) const
    {
        return mapId == MAP_ID && EncounterHelpers::HasEnemyEntry(world, NPC_FLAMELASH);
    }

    void BlackrockDepthsFlamelash::OnReset()
    {
        *this = BlackrockDepthsFlamelash();
    }

    void BlackrockDepthsFlamelash::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        EnemyFacts const* boss = EncounterHelpers::FindEnemyByEntry(world, NPC_FLAMELASH);
        m_boss = boss ? boss->guid : ObjectGuid();
        m_spirits.clear();
        if (!boss || !bot || !bot->me->IsInWorld())
            return;
        Creature* flamelash = bot->me->GetMap()->GetCreature(m_boss);
        if (!flamelash)
            return;
        std::list<Creature*> spirits;
        flamelash->GetCreatureListWithEntryInGrid(spirits, NPC_BURNING_SPIRIT, SPIRIT_SEARCH_YD);
        for (Creature* c : spirits)
            if (c->IsAlive())
                m_spirits.push_back(c->GetObjectGuid());

        if (!m_spirits.empty() && !m_spiritsNoted)
        {
            m_spiritsNoted = true;
            Note("Flamelash: Burning Spirits run to him from the runes, each one he takes makes him hit harder. "
                 "Damage dealers kill them on the way, the tank stays on him.");
            Say(EncounterHelpers::Line("voice.boss.flamelash.spirits", VoiceTier::Urgent));
        }
    }

    void BlackrockDepthsFlamelash::ListEnemies(TeamBotAI* bot, WorldModel const& /*world*/, std::vector<Creature*>& out) const
    {
        if (!bot || !bot->me->IsInWorld())
            return;
        Map* map = bot->me->GetMap();
        for (ObjectGuid const& guid : m_spirits)
            if (Creature* c = map->GetCreature(guid))
                out.push_back(c);
    }

    void BlackrockDepthsFlamelash::OfferDuties(WorldModel const& world, TeamPlan const& /*plan*/,
                                               std::vector<DutyNeed>& needs) const
    {
        EnemyFacts const* boss = world.FindEnemy(m_boss);
        if (!boss)
            return;

        // What one more stack costs: his extra damage on whoever he hits, for the rest of the fight.
        float const swingSec = boss->meleeDps > 0.0f ? boss->meleeHit / boss->meleeDps : 2.0f;
        float const fightSec = std::max(MIN_FIGHT_SEC, float(boss->health) / std::max(1.0f, Value::DealtDps(world)));
        float const points = Value::Points(world, STACK_DAMAGE / std::max(0.5f, swingSec) * fightSec);

        for (EnemyFacts const& e : world.targeted)
        {
            if (!e.listed || e.entry != NPC_BURNING_SPIRIT || !e.health)
                continue;
            DutyKey const handled{ DutyKeyKind::EnemyHandled, e.guid };
            DutyNeed need;
            need.kind = DutyKind::Intercept;
            need.target = e.guid;
            need.now = true;
            need.points = points;
            need.reason = "flamelash: burning spirit on its way to him";
            need.blockedBy.push_back(handled);
            need.marks.push_back(handled);
            // Equal points: the one closest to him first.
            need.tieMs = uint32(PackResolver::Distance(e.pos, boss->pos) * 1000.0f);

            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || !m.DealsDamage() || !m.CanAct() || m.guid == boss->victim)
                    continue;
                bool const ranged = m.role != ROLE_MELEE_DPS && m.role != ROLE_TANK;
                float const range = ranged ? FocusAssigner::INTERCEPT_RANGE : FocusAssigner::MELEE_INTERCEPT_RANGE;
                float const d = PackResolver::Distance(m.pos, e.pos);
                if (d > range)
                    continue;
                DutyCandidate c;
                c.bot = m.guid;
                c.rank = (ranged ? RANGED_FIRST_RANK : 0.0f) - d;
                need.candidates.push_back(c);
            }
            if (!need.candidates.empty())
                needs.push_back(std::move(need));
        }
    }
}
