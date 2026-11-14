#include "Commander/Behaviors/Combat/AreaRoomBehavior.h"
#include "Commander/Behaviors/Movement/EscapeBehavior.h"
#include "Commander/Behaviors/Movement/RunToTank.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Spatial/LeapSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "DamageSpells.h"
#include "DBCStores.h"
#include "Map.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <cmath>
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

        // Seconds until a self spell of the bot can be cast, counted only within a global
        // cooldown (header "Ready"): 0 now, < 0 not soon (a longer cooldown or anything else).
        float ReadyIn(PartyBotAI const* bot, SpellEntry const* spell)
        {
            float const sec = spell ? bot->SpellReadyInSec(bot->me, spell) : -1.0f;
            return sec <= Value::GCD_SEC ? sec : -1.0f;
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
            // Seconds until the Frost Nova can be cast (ReadyIn): 0 now, < 0 not soon.
            float novaInSec = -1.0f;
            float holdSec = 0.0f;

            // Header: held(T) - wait(T); `alive` false: T is out (danger).
            float Worth(WorldModel const& world, float t, bool& alive, bool& dies) const
            {
                float held = 0.0f, damage = 0.0f, rate = 0.0f;
                for (Runner const& r : runners)
                {
                    EnemyFacts const& e = *r.facts;
                    if (novaInSec >= 0.0f && t >= novaInSec && r.inRadiusSec <= t)
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
                if (novaInSec > 0.0f && novaInSec <= horizon)
                    moments.push_back(novaInSec);
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
                line.novaInSec = radius > 0.0f ? ReadyIn(bot, nova) : -1.0f;
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

        // Header "How" 2: Blink away (LeapSpots), the landing from which its ground channel
        // still reaches the first runner first, then nearest a tank (none: farthest from the
        // runners). False: not ready or nowhere to land.
        bool ProposeBlink(PartyBotAI const* bot, WorldModel const& world, Line const& line, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* blink = bot->Kit().Leap();
            if (!blink || !bot->CanTryToCastSpell(me, blink))
                return false;
            float distance = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(blink->EffectRadiusIndex[i]))
                    distance = std::max(distance, r->Radius);
            if (distance <= 0.0f)
                return false;

            Position3 centroid;
            for (Runner const& r : line.runners)
            {
                centroid.x += r.facts->pos.x / float(line.runners.size());
                centroid.y += r.facts->pos.y / float(line.runners.size());
            }
            std::vector<LeapSpots::Spot> const spots = LeapSpots::Find(me, world, distance, centroid);
            if (spots.empty())
                return false;

            float reach = 0.0f;
            for (AreaSpellFacts const& a : line.self->areaSpells)
                if (a.onTarget && a.channel)
                    reach = std::max(reach, a.range - RB::REACH_MARGIN_YD);
            Position3 const& lead = line.runners.front().facts->pos;
            PartyMemberFacts const* tank = Tanks::Main(world);
            auto const gap = [&](Position3 const& p)
            {
                float g = 1e9f;
                for (Runner const& r : line.runners)
                    g = std::min(g, PackResolver::Distance(p, r.facts->pos));
                return g;
            };
            LeapSpots::Spot const* best = nullptr;
            bool bestReaches = false;
            float bestScore = 0.0f;
            for (LeapSpots::Spot const& s : spots)
            {
                bool const reaches = reach > 0.0f && PackResolver::Distance(s.landing, lead) <= reach;
                float const score = tank ? -PackResolver::Distance(s.landing, tank->pos) : gap(s.landing);
                if (!best || (reaches && !bestReaches) || (reaches == bestReaches && score > bestScore))
                {
                    best = &s;
                    bestReaches = reaches;
                    bestScore = score;
                }
            }

            // Worth: the hits of the runners while they walk the distance again.
            float saved = 0.0f;
            for (Runner const& r : line.runners)
            {
                float const sec = distance / std::max(1.0f, r.facts->speed);
                saved += Value::HitLoss(world, *r.facts, *line.self, sec) + Value::HarassLoss(world, *r.facts, *line.self, sec);
            }
            Proposal p;
            p.tier = ProposalTier::Urgent;
            p.source = SOURCE;
            p.reason = "they are here, Frost Nova cannot hold them all: Blink away";
            p.action.type = BotActionType::Cast;
            p.action.spell = blink;
            p.action.facing = best->orientation;
            float const mana = DamageSpells::ManaCost(me, blink);
            p.SetValue(saved, Value::LostAction(world, *line.self, Value::GCD_SEC) +
                              (mana > 0.0f ? ManaWorth::Points(world, *line.self, mana) : 0.0f));
            out.push_back(p);
            return true;
        }

        bool CanCast(PartyBotAI const* bot, SpellEntry const* spell)
        {
            return spell && bot->CanTryToCastSpell(bot->me, spell);
        }

        // Header "Cone of Cold": turn to the nearest runner not slowed yet within its radius
        // (`inFace`: only those about to hit, ArriveSec within ACT_SEC) and slow it and every
        // other one inside the cone. False: not ready or nobody to slow.
        bool ProposeCone(PartyBotAI const* bot, WorldModel const& world, Line const& line, bool inFace,
                         char const* reason, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* cone = bot->Kit().SlowCone();
            if (!CanCast(bot, cone))
                return false;
            float const radius = RadiusOf(me, cone) - RB::RADIUS_MARGIN_YD;
            float const halfArc = sSpellMgr.GetSpellCone(cone->Id) * 0.5f;
            float slowPct = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (cone->Effect[i] == SPELL_EFFECT_APPLY_AURA && cone->EffectApplyAuraName[i] == SPELL_AURA_MOD_DECREASE_SPEED)
                    slowPct = std::max(slowPct, std::min(1.0f, -float(cone->CalculateSimpleValue(SpellEffectIndex(i))) / 100.0f));
            float const slowSec = std::min(EB::ROOT_HOLD_SEC, std::max(0, cone->GetDuration()) / 1000.0f * slowPct);
            if (radius <= 0.0f || slowSec <= 0.0f)
                return false;

            Position3 const& at = line.self->pos;
            auto const inReach = [&](EnemyFacts const& e)
            {
                return !e.slowed && PackResolver::Distance(at, e.pos) - e.boundingRadius <= radius;
            };
            EnemyFacts const* aim = nullptr;
            for (Runner const& r : line.runners)
                if (inReach(*r.facts) && (!inFace || r.inMeleeSec <= RB::ACT_SEC))
                {
                    aim = r.facts;
                    break;      // nearest first
                }
            if (!aim)
                return false;
            float o = std::atan2(aim->pos.y - at.y, aim->pos.x - at.x);
            if (o < 0.0f)
                o += 2.0f * float(M_PI);

            float saved = 0.0f;
            for (Runner const& r : line.runners)
            {
                EnemyFacts const& e = *r.facts;
                if (!inReach(e))
                    continue;
                float d = std::fabs(std::atan2(e.pos.y - at.y, e.pos.x - at.x) - o);
                if (d > float(M_PI))
                    d = 2.0f * float(M_PI) - d;
                if (&e != aim && d > halfArc)
                    continue;
                saved += Value::HitLoss(world, e, *line.self, slowSec) + Value::HarassLoss(world, e, *line.self, slowSec);
            }
            Proposal p;
            p.tier = ProposalTier::Urgent;
            p.source = SOURCE;
            p.reason = reason;
            p.action.type = BotActionType::Cast;
            p.action.spell = cone;
            p.action.facing = o;
            p.action.jumpTurn = true;   // running to the tank: jump turn, no stop (JumpCast.h)
            float const mana = DamageSpells::ManaCost(me, cone);
            p.SetValue(saved, Value::LostAction(world, *line.self, Value::GCD_SEC) +
                              (mana > 0.0f ? ManaWorth::Points(world, *line.self, mana) : 0.0f));
            out.push_back(p);
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
        if (plan.retreat.active)
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
        // In the air (jump turn): nothing else until it lands.
        if (bot->IsJumpCasting())
        {
            Proposal hold;
            hold.tier = ProposalTier::Urgent;
            hold.source = SOURCE;
            hold.reason = "jump turn";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }
        // Frost Nova spent (on cooldown, or some rooted next to it): the ones still coming are
        // slowed as they get near (doc 3.5); on the way to the tank with a jump turn.
        bool const novaSpent = line.novaInSec < 0.0f || RootedOnMe(world, line.self->guid);
        if (novaSpent && ProposeCone(bot, world, line, false, "chased, no Frost Nova: Cone of Cold", out))
            return;

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
        bool const rooted = me->HasUnitState(UNIT_STATE_ROOT);

        // Right after a Frost Nova that left some free: to the tank now (doc 3.4 step 3).
        if (line.novaInSec < 0.0f && RootedOnMe(world, line.self->guid))
        {
            if (!rooted)
                ProposeToTank(bot, world, line, "Frost Nova did not hold them all: to the tank", out);
            return;
        }

        float worth = 0.0f;
        if (line.Best(world, HORIZON_SEC, worth) > ACT_SEC)
            return;     // a better moment comes: go on (header "When")

        // Within the Frost Nova's radius now, and what holding them is worth.
        float held = 0.0f;
        uint32 within = 0;
        for (Runner const& r : line.runners)
            if (r.inRadiusSec <= ACT_SEC)
            {
                ++within;
                held += Value::HitLoss(world, *r.facts, *line.self, line.holdSec) +
                        Value::HarassLoss(world, *r.facts, *line.self, line.holdSec);
            }
        auto const proposeNova = [&](char const* reason)
        {
            Proposal p;
            p.tier = ProposalTier::Urgent;
            p.source = SOURCE;
            p.reason = reason;
            p.action.type = BotActionType::Cast;
            p.action.spell = nova;
            float const mana = DamageSpells::ManaCost(me, nova);
            p.SetValue(held, Value::LostAction(world, *line.self, Value::GCD_SEC) +
                             (mana > 0.0f ? ManaWorth::Points(world, *line.self, mana) : 0.0f));
            out.push_back(p);
        };
        // A step's spell still in the global cooldown (header "Ready"): wait for it when the
        // caster lives that long, rather than drop to the next step (strat-20261114-180150: the
        // Nova was 1.4 s off, the mage walked to the tank under the hits instead).
        auto const waits = [&](float inSec)
        {
            if (inSec <= 0.0f)
                return false;
            bool alive = true, dies = false;
            line.Worth(world, inSec, alive, dies);
            return alive;
        };
        bool const novaSoon = nova && within > 0 && line.novaInSec >= 0.0f;

        // 1. Frost Nova holds all of them.
        if (novaSoon && within == line.runners.size())
        {
            if (line.novaInSec <= 0.0f)
                proposeNova("they are here, all within reach: Frost Nova");
            if (line.novaInSec <= 0.0f || waits(line.novaInSec))
                return;
        }
        // 2. It cannot: Blink away (its Blizzard goes ahead of the first one again, AreaPlace);
        // the one or two about to hit get a Cone of Cold first (user 2026-11-12: some room).
        float const blinkInSec = ReadyIn(bot, bot->Kit().Leap());
        if (blinkInSec == 0.0f)
        {
            if (ProposeCone(bot, world, line, true, "about to be hit: Cone of Cold before the Blink", out))
                return;
            if (ProposeBlink(bot, world, line, out))
                return;
        }
        else if (waits(blinkInSec))
            return;
        // 3. Frost Nova what it can, then to the tank.
        if (novaSoon)
        {
            if (line.novaInSec <= 0.0f)
                proposeNova("they are here, no Blink: Frost Nova what it can");
            if (line.novaInSec <= 0.0f || waits(line.novaInSec))
                return;
        }
        if (!rooted)
            ProposeToTank(bot, world, line, "they are here, no Frost Nova: to the tank", out);
    }

    float AreaRoomBehavior::CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                             uint32 castLeftMs, char const*& reason) const
    {
        if (plan.retreat.active)
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
        reason = "they are here: get room now, not after the cast";
        return std::max(0.0f, now - atEnd) + (dies ? Value::DeathPenalty(*line.self) : 0.0f);
    }
}
