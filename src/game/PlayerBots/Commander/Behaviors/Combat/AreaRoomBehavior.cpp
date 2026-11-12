#include "Commander/Behaviors/Combat/AreaRoomBehavior.h"
#include "Commander/Behaviors/Movement/EscapeBehavior.h"
#include "Commander/Behaviors/Movement/RunToTank.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "DamageSpells.h"
#include "DBCStores.h"
#include "Map.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstring>

namespace Commander
{
    namespace
    {
        using RB = AreaRoomBehavior;
        using EB = EscapeBehavior;
        char const* const SOURCE = "aoe-room";

        // The class kit's hold around the bot (Frost Nova), null = none.
        SpellEntry const* NovaOf(PartyBotAI const* bot)
        {
            for (SpellEntry const* s : bot->Kit().HoldSpells())
                if (s && s->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY)
                    return s;
            return nullptr;
        }

        // Its radius with the caster's talents (Arctic Reach), as Spell::FillTargetMap.
        float RadiusOf(Player* me, SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            me->ApplySpellMod(spell->Id, SPELLMOD_RADIUS, radius, nullptr, false);
            return radius;
        }

        struct Runner
        {
            EnemyFacts const* facts = nullptr;
            float inRadiusSec = 0.0f;
            float inMeleeSec = 0.0f;
        };

        // The time line (header "When").
        struct Line
        {
            PartyMemberFacts const* self = nullptr;
            std::vector<Runner> runners;
            bool novaReady = false;
            float holdSec = 0.0f;

            // Header: held(T) - wait(T); `alive` false: T is out (danger).
            float Worth(WorldModel const& world, float t, bool& alive, bool& dies) const
            {
                float held = 0.0f, damage = 0.0f, rate = 0.0f;
                for (Runner const& r : runners)
                {
                    EnemyFacts const& e = *r.facts;
                    if (novaReady && r.inRadiusSec <= t)
                        held += Value::HitLoss(world, e, *self, holdSec) + Value::HarassLoss(world, e, *self, holdSec);
                    if (r.inMeleeSec < t)
                    {
                        damage += e.meleeDps * (t - r.inMeleeSec);
                        rate += e.meleeDps;
                    }
                }
                float const left = float(self->health) + self->absorb - damage;
                dies = left <= 0.0f;
                alive = !dies && (rate <= 0.0f || left / rate >= EB::DANGER_SEC);
                float const taken = std::max(0.0f, damage - self->absorb);
                return held - (taken > 0.0f ? Value::Points(world, taken) : 0.0f);
            }

            // The best moment within `horizon` (later on a tie); its worth.
            float Best(WorldModel const& world, float horizon, float& worth) const
            {
                std::vector<float> moments{ 0.0f };
                for (Runner const& r : runners)
                {
                    if (r.inRadiusSec <= horizon)
                        moments.push_back(r.inRadiusSec);
                    if (r.inMeleeSec <= horizon)
                        moments.push_back(r.inMeleeSec);
                }
                float best = 0.0f;
                bool alive = true, dies = false;
                worth = Worth(world, 0.0f, alive, dies);
                for (float t : moments)
                {
                    float const v = Worth(world, t, alive, dies);
                    if (!alive)
                        continue;
                    if (v > worth + 0.001f || (v >= worth - 0.001f && t > best))
                    {
                        best = t;
                        worth = v;
                    }
                }
                return best;
            }
        };

        bool Build(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan, Line& line, SpellEntry const*& nova)
        {
            Player* me = bot->me;
            if (plan.area.roomCaster != me->GetObjectGuid())
                return false;
            PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
            if (!self || !self->CanAct())
                return false;
            line.self = self;
            nova = NovaOf(bot);
            float radius = 0.0f;
            if (nova)
            {
                radius = RadiusOf(me, nova);
                line.novaReady = radius > 0.0f && bot->CanTryToCastSpell(me, nova);
                line.holdSec = std::min(EB::ROOT_HOLD_SEC, std::max(0, nova->GetDuration()) / 1000.0f);
            }
            // Its own area channel slows them on the way.
            AreaSpellFacts const* area = self->areaChannel.spellId ? AreaValue::SpellOf(*self, self->areaChannel.spellId) : nullptr;
            Position3 const centre = self->areaChannel.centre;
            for (Runners::Runner const& r : Runners::Of(world, self->guid, self->pos))
            {
                Runner x;
                x.facts = r.facts;
                x.inRadiusSec = Runners::ReachSec(*r.facts, self->pos, radius + r.facts->boundingRadius - RB::RADIUS_MARGIN_YD,
                                                  area, centre);
                x.inMeleeSec = Runners::ArriveSec(*r.facts, self->pos, area, centre);
                line.runners.push_back(x);
            }
            return true;
        }

        // Enemies rooted next to the bot that hit it (its own Frost Nova just now).
        bool RootedOnMe(WorldModel const& world, ObjectGuid self)
        {
            return std::any_of(world.enemies.begin(), world.enemies.end(),
                               [self](EnemyFacts const& e) { return e.acting && e.rooted && e.victim == self; });
        }

        void ProposeToTank(PartyBotAI const* bot, WorldModel const& world, Line const& line, char const* reason,
                           std::vector<Proposal>& out)
        {
            std::vector<EnemyFacts const*> chasers;
            for (Runner const& r : line.runners)
                chasers.push_back(r.facts);
            Unit* from = bot->me->GetMap()->GetUnit(chasers.front()->guid);
            float const incoming = Value::IncomingOn(world, line.self->guid);
            float const absorbedSec = incoming > 0.0f ? line.self->absorb / incoming : 0.0f;
            std::size_t const before = out.size();
            if (RunToTank::Propose(bot, world, *line.self, chasers, from, absorbedSec, ProposalTier::Urgent, SOURCE, out) &&
                out.size() > before)
                out.back().reason = reason;
        }
    }

    bool AreaRoomBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_RANGE_DPS && NovaOf(bot) != nullptr;
    }

    void AreaRoomBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (plan.retreat.active || bot->IsInDuel())
            return;
        Line line;
        SpellEntry const* nova = nullptr;
        if (!Build(bot, world, plan, line, nova))
            return;
        Player* me = bot->me;
        if (line.runners.empty())
        {
            RunToTank::ProposeStop(bot, SOURCE, "nobody running at me any more, stop", out);
            return;
        }
        // Its own walk to the tank is under way: keep walking.
        char const* owner = bot->Directives().GetMovementOwner(WorldTimer::getMSTime());
        if (owner && !strcmp(owner, SOURCE) && me->IsMoving())
        {
            Proposal hold;
            hold.tier = ProposalTier::Urgent;
            hold.source = SOURCE;
            hold.reason = "on the way to the tank";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }
        if (me->HasUnitState(UNIT_STATE_ROOT))
            return;

        // Right after a Frost Nova that left some free: to the tank now (doc 3.4 step 3).
        if (!line.novaReady && RootedOnMe(world, line.self->guid))
        {
            ProposeToTank(bot, world, line, "Frost Nova did not hold them all: to the tank", out);
            return;
        }

        float worth = 0.0f;
        if (line.Best(world, HORIZON_SEC, worth) > ACT_SEC)
            return;     // a better moment comes: go on (header "When")

        // 1. Frost Nova what is within its radius now.
        if (line.novaReady && nova)
        {
            float held = 0.0f;
            bool any = false;
            for (Runner const& r : line.runners)
                if (r.inRadiusSec <= ACT_SEC)
                {
                    any = true;
                    held += Value::HitLoss(world, *r.facts, *line.self, line.holdSec) +
                            Value::HarassLoss(world, *r.facts, *line.self, line.holdSec);
                }
            if (any)
            {
                Proposal p;
                p.tier = ProposalTier::Urgent;
                p.source = SOURCE;
                p.reason = "they are here: Frost Nova";
                p.action.type = BotActionType::Cast;
                p.action.spell = nova;
                float const mana = DamageSpells::ManaCost(me, nova);
                p.SetValue(held, Value::LostAction(world, *line.self, Value::GCD_SEC) +
                                 (mana > 0.0f ? ManaWorth::Points(world, *line.self, mana) : 0.0f));
                out.push_back(p);
                return;
            }
        }

        // 3. Otherwise to the tank.
        ProposeToTank(bot, world, line, "they are here, no Frost Nova: to the tank", out);
    }

    float AreaRoomBehavior::CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                             uint32 castLeftMs, char const*& reason) const
    {
        if (plan.retreat.active || bot->IsInDuel())
            return 0.0f;
        Line line;
        SpellEntry const* nova = nullptr;
        if (!Build(bot, world, plan, line, nova) || line.runners.empty())
            return 0.0f;
        float const leftSec = castLeftMs / 1000.0f;
        float now = 0.0f;
        if (line.Best(world, std::min(HORIZON_SEC, leftSec), now) > ACT_SEC)
            return 0.0f;
        bool alive = true, dies = false;
        float const atEnd = line.Worth(world, leftSec, alive, dies);
        reason = line.novaReady ? "they are here: Frost Nova now, not after the cast"
                                : "they are here: to the tank now, not after the cast";
        return std::max(0.0f, now - atEnd) + (dies ? Value::DeathPenalty(*line.self) : 0.0f);
    }
}
