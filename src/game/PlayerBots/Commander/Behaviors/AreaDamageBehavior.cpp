#include "Commander/Behaviors/AreaDamageBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "DBCStores.h"
#include "Map.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using AB = AreaDamageBehavior;

        float AreaRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            return radius;
        }

        Position3 PositionOf(Unit const* unit)
        {
            Position3 p;
            p.x = unit->GetPositionX();
            p.y = unit->GetPositionY();
            p.z = unit->GetPositionZ();
            return p;
        }

        // How the damage comes: a channel (Blizzard) in `count` ticks every `periodSec`, anything
        // else in one go after `periodSec` = the busy time.
        struct Ticks
        {
            uint32 count = 1;
            float periodSec = 0.0f;
        };

        Ticks TicksOf(Unit const* caster, SpellEntry const* spell)
        {
            Ticks t;
            t.periodSec = DamageSpells::BusySec(caster, spell);
            if (!spell->IsChanneledSpell())
                return t;
            int32 const duration = spell->GetDuration();
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->EffectAmplitude[i] == 0 || duration <= 0)
                    continue;
                t.count = std::max<uint32>(1, uint32(duration) / spell->EffectAmplitude[i]);
                t.periodSec = spell->EffectAmplitude[i] / 1000.0f;
                break;
            }
            return t;
        }

        // Threat one tick of `damage` gives the caster (spell multiplier, caster modifiers).
        float ThreatOf(Player* me, SpellEntry const* spell, float damage)
        {
            return me->ApplyTotalThreatModifier(damage * sSpellMgr.GetSpellThreatMultiplier(spell),
                                                spell->GetSpellSchoolMask());
        }

        struct Judgement
        {
            uint32 targets = 0;
            uint32 safeTicks = 0;       // ticks before the first enemy would turn on the bot
            float damage = 0.0f;        // over all targets for safeTicks ticks, each at most its health
        };

        // The area spell centred on `centre`: who it hits and for how many ticks it is safe.
        Judgement Judge(PartyBotAI const* bot, WorldModel const& world, SpellEntry const* spell,
                        Position3 const& centre, float perTarget, Ticks const& ticks)
        {
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();
            Position3 const here = PositionOf(me);
            float const radius = AreaRadius(spell);
            float const perTick = perTarget / float(ticks.count);
            float const tickThreat = ThreatOf(me, spell, perTick);
            uint32 const settleMs = uint32(ThreatBehavior::NEW_ENEMY_SETTLE_SEC * 1000.0f);

            Judgement j;
            j.safeTicks = ticks.count;
            std::vector<float> healths;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || PackResolver::Distance(centre, e.pos) - e.boundingRadius > radius)
                    continue;
                // Polymorph, sap, a stun that breaks on damage: the area spell wakes it up.
                if (e.crowdControlled)
                {
                    j.safeTicks = 0;
                    continue;
                }
                ++j.targets;
                healths.push_back(float(e.health));
                if (e.victim == self || e.victim.IsEmpty())
                    continue;
                // The tank has not built threat on a new enemy yet.
                if (e.firstSeenMs && WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs) < settleMs)
                {
                    j.safeTicks = 0;
                    continue;
                }
                // Tick k is safe while my threat stays under its victim's threat x 110% (in its
                // melee reach) / 130%, the victim gaining TANK_GAIN_SHARE of its measured rate.
                ThreatEntry const* mine = ThreatView::FindEntry(e, self);
                float const myThreat = mine ? mine->threat : 0.0f;
                bool const melee = PackResolver::Distance(here, e.pos) - e.boundingRadius <= AB::MELEE_REACH;
                float const factor = melee ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                float rate = 0.0f;
                if (ThreatEntry const* holder = ThreatView::FindEntry(e, e.victim))
                    rate = holder->threatPerSec * AB::TANK_GAIN_SHARE;
                uint32 safe = 0;
                while (safe < j.safeTicks)
                {
                    float const k = float(safe + 1);
                    if (myThreat + k * tickThreat > (e.victimThreat + rate * k * ticks.periodSec) * factor)
                        break;
                    ++safe;
                }
                j.safeTicks = safe;
            }
            for (float health : healths)
                j.damage += std::min(perTick * float(j.safeTicks), health);
            return j;
        }
    }

    bool AreaDamageBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        CombatBotRoles const role = bot->GetRole();
        return (role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS) && !bot->Kit().AreaDamage().empty();
    }

    bool AreaDamageBehavior::Judges(PartyBotAI const* bot, SpellEntry const* spell)
    {
        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("aoe"))
            return false;
        // This module proposes area spells in combat only.
        if (!bot->me->IsInCombat())
            return false;
        uint32 const first = sSpellMgr.GetFirstSpellInChain(spell->Id);
        for (AreaDamageSpell const& a : bot->Kit().AreaDamage())
            if (a.spell && sSpellMgr.GetFirstSpellInChain(a.spell->Id) == first)
                return true;
        return false;
    }

    void AreaDamageBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit* victim = me->GetVictim();
        if (!victim || world.enemies.empty() || !me->IsInCombat())
            return;
        // A walk of another module is under way (escape, position): a cast would stop it.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        // The filler on the victim: what the area spell is compared with.
        SpellEntry const* filler = bot->Kit().Filler();
        if (!filler)
            return;
        float const fillerDamage = DamageSpells::Estimate(me, victim, filler);
        float const fillerMana = DamageSpells::ManaCost(me, filler);
        float const fillerPerSec = fillerDamage / DamageSpells::BusySec(me, filler);
        float const fillerPerMana = fillerMana > 0.0f ? fillerDamage / fillerMana : 0.0f;
        if (fillerPerSec <= 0.0f)
            return;

        Map* map = me->GetMap();
        for (AreaDamageSpell const& area : bot->Kit().AreaDamage())
        {
            SpellEntry const* spell = area.spell;
            if (!spell)
                continue;
            Ticks const ticks = TicksOf(me, spell);
            if (ticks.count > 1 && me->IsMoving())
                continue;
            float const perTarget = DamageSpells::Estimate(me, victim, spell);
            float const mana = DamageSpells::ManaCost(me, spell);

            // Where: every enemy as the centre of a ground spell (Blizzard), else the bot.
            std::vector<Unit*> centres;
            if (area.onTarget)
            {
                for (EnemyFacts const& e : world.enemies)
                    if (e.acting && !e.crowdControlled)
                        if (Unit* unit = map->GetUnit(e.guid))
                            if (unit->IsAlive() && bot->IsValidHostileTarget(unit) && me->IsWithinLOSInMap(unit))
                                centres.push_back(unit);
            }
            else
                centres.push_back(nullptr);

            Judgement best;
            Unit* bestCentre = nullptr;
            bool found = false;
            for (Unit* centre : centres)
            {
                // Range, mana, cooldown, idle creatures in the area (PartyBotAI::PassesPartyCastChecks).
                if (!bot->CanTryToCastSpell(centre ? centre : me, spell))
                    continue;
                Judgement const j = Judge(bot, world, spell, PositionOf(centre ? centre : me), perTarget, ticks);
                if (j.safeTicks == 0 || j.targets == 0)
                    continue;
                if (!found || j.damage > best.damage)
                {
                    best = j;
                    bestCentre = centre;
                    found = true;
                }
            }
            if (!found)
                continue;

            // Not more damage per mana than the filler: no fight length guess needed (adds).
            if (mana > 0.0f && fillerPerMana > 0.0f && best.damage / mana < fillerPerMana)
                continue;

            // Worth: the damage as seconds of the filler, against the seconds spent on it.
            float const busySec = float(best.safeTicks) * ticks.periodSec;
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "aoe";
            p.reason = best.safeTicks < ticks.count ? "area damage, channel until it would pull aggro"
                                                    : "area damage beats the filler";
            p.action.type = BotActionType::Cast;
            p.action.spell = spell;
            // Ground spells on the chosen enemy; the others on the bot itself (a cone goes the
            // way the bot faces: its victim, the filler turned it there).
            if (bestCentre)
                p.action.target = bestCentre->GetObjectGuid();
            p.SetValue(Value::LostAction(world, *self, best.damage / fillerPerSec),
                       Value::LostAction(world, *self, busySec));
            out.push_back(p);
        }
    }

    bool AreaDamageBehavior::WatchChannel(PartyBotAI* bot, uint32& lastCheckMs)
    {
        Player* me = bot->me;
        Spell* channel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!channel || channel->getState() != SPELL_STATE_CASTING)
            return false;
        uint32 const now = WorldTimer::getMSTime();
        if (WorldTimer::getMSTimeDiff(lastCheckMs, now) < CHANNEL_CHECK_MS)
            return false;
        lastCheckMs = now;

        SpellEntry const* spell = channel->m_spellInfo;
        if (!Judges(bot, spell) || bot->IsInDuel())
            return false;
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return false;
        WorldModel const& world = commander->GetWorld();

        Position3 centre = PositionOf(me);
        float x = 0.0f, y = 0.0f, z = 0.0f;
        channel->m_targets.getDestination(x, y, z);
        if (x != 0.0f || y != 0.0f)
        {
            centre.x = x;
            centre.y = y;
            centre.z = z;
        }
        float const radius = AreaRadius(spell);
        Ticks const ticks = TicksOf(me, spell);

        // Live threat: the next tick must not make an enemy in the area turn on the bot.
        Map* map = me->GetMap();
        for (EnemyFacts const& e : world.enemies)
        {
            Unit* unit = map->GetUnit(e.guid);
            if (!unit || !unit->IsAlive() || !unit->CanHaveThreatList())
                continue;
            if (PackResolver::Distance(centre, PositionOf(unit)) - unit->GetObjectBoundingRadius() > radius)
                continue;
            Unit* holder = unit->GetVictim();
            if (!holder || holder == me)
                continue;
            float const tick = ThreatOf(me, spell, DamageSpells::Estimate(me, unit, spell) / float(ticks.count));
            float const factor = unit->CanReachWithMeleeAutoAttack(me) ? ThreatView::MELEE_AGGRO_FACTOR
                                                                       : ThreatView::RANGED_AGGRO_FACTOR;
            ThreatManager& threat = unit->GetThreatManager();
            if (threat.getThreat(me) + tick > threat.getThreat(holder) * factor)
            {
                me->InterruptSpell(CURRENT_CHANNELED_SPELL, true);
                return true;
            }
        }
        return false;
    }
}
