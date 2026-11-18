#include "Commander/Behaviors/Combat/FinishCastBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // The bot's single target damage cast in progress whose target the others kill before
        // it lands (header): that target, else null.
        EnemyFacts const* WastedTarget(TeamBotAI const* bot, WorldModel const& world)
        {
            Player* me = bot->me;
            Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || spell->getState() != SPELL_STATE_PREPARING || spell->GetCastTime() <= 0)
                return nullptr;
            SpellEntry const* info = spell->m_spellInfo;
            if (!info || info->IsAreaOfEffectSpell() || !SpellTags::DealsDamage(info))
                return nullptr;
            EnemyFacts const* enemy = world.FindEnemy(spell->m_targets.getUnitTargetGuid());
            PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
            if (!enemy || !self)
                return nullptr;
            uint32 const sinceRefresh = WorldTimer::getMSTimeDiff(world.refreshedAtMs, WorldTimer::getMSTime());
            uint32 const landMs = sinceRefresh + uint32(std::max(0, int32(spell->GetCastedTime()))) +
                                  FinishForecast::FlightMs(self->pos, *enemy, info->speed);
            return FinishForecast::Finished(world, *enemy, self->guid, landMs) ? enemy : nullptr;
        }
    }

    bool FinishCastBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_RANGE_DPS;
    }

    float FinishCastBehavior::CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                               uint32 castLeftMs, char const*& reason) const
    {
        EnemyFacts const* wasted = WastedTarget(bot, world);
        if (!wasted)
            return 0.0f;
        ObjectGuid const self = bot->me->GetObjectGuid();
        PartyMemberFacts const* facts = world.FindMember(self);
        if (!facts)
            return 0.0f;
        TargetDuty const* duty = plan.FindTargetFor(self);
        reason = duty && !duty->enemy.IsEmpty() && duty->enemy != wasted->guid
            ? "the others kill its target first: on to the next"
            : "the others kill its target first: an instant spell instead";
        return Value::LostAction(world, *facts, float(castLeftMs) / 1000.0f);
    }

    float FinishCastBehavior::ContinueValue(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& /*plan*/,
                                            SpellEntry const* spell, char const*& reason) const
    {
        Spell const* current = bot->me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!current || current->m_spellInfo != spell || !WastedTarget(bot, world))
            return -1.0f;
        reason = "lands on a dead enemy";
        return 0.0f;
    }
}
