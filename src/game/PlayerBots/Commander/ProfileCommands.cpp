#include "Commander/ProfileCommands.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"

namespace Commander
{
namespace ProfileCommands
{
    namespace
    {
        std::string SpellName(uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            return spell ? spell->SpellName[0] : "?";
        }

        char const* TargetTypeName(uint8 castTarget)
        {
            switch (castTarget)
            {
                case 0:    return "provided";
                case 1:    return "victim";
                case 2:    return "2nd-threat";
                case 3:    return "last-threat";
                case 4:    return "random";
                case 5:    return "random-not-top";
                case 6:    return "nearest";
                case 8:    return "self";
                case 0xFF: return "-";
                default:   return "other";
            }
        }

        char const* ResultName(CandidateRecord::Result result)
        {
            switch (result)
            {
                case CandidateRecord::Result::NotTried:  return "not tried";
                case CandidateRecord::Result::Executed:  return "EXECUTED";
                case CandidateRecord::Result::Failed:    return "failed";
                case CandidateRecord::Result::LowerTier: return "lower tier";
                case CandidateRecord::Result::Dropped:   return "dropped (benefit <= cost)";
                case CandidateRecord::Result::VetoSet:   return "VETO";
            }
            return "?";
        }

        void ShowSpellLine(ChatHandler* handler, uint32 spellId, uint32 casterLevel, float meleeHit, char const* prefix)
        {
            std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(spellId);
            if (!p)
            {
                handler->PSendSysMessage("%s%u: unknown spell", prefix, spellId);
                return;
            }
            SpellAmounts const a = SpellProfiles::Estimate(sSpellMgr.GetSpellEntry(spellId), casterLevel, meleeHit);
            handler->PSendSysMessage("%s%u %s: %s, cast %u ms, dur %u ms, radius %.0f, %s%s, dispel %s%s",
                prefix, spellId, SpellName(spellId).c_str(), SpellTags::CategoryName(p->category),
                p->castMs, p->durationMs, p->radius,
                p->interruptible ? "interruptible" : "not interruptible",
                p->controls ? ", control" : "",
                SpellProfiles::DispelName(p->dispel), p->harmful ? "" : " (positive)");
            handler->PSendSysMessage("%s    at level %u: damage %.0f + %.0f over time, heal %.0f + %.0f, control %.1f s",
                prefix, casterLevel, a.damage, a.periodicDamage, a.heal, a.periodicHeal, a.controlSec);
            if (p->hasOverride)
                handler->PSendSysMessage("%s    override: interrupt x%.2f, dispel %s  %s", prefix,
                    p->override.interruptMult, SpellProfiles::PolicyName(p->override.dispel), p->override.comment.c_str());
        }
    }

    bool ShowCreature(ChatHandler* handler, Creature* creature)
    {
        if (!creature)
        {
            handler->SendSysMessage("Select a creature first.");
            return false;
        }

        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(creature->GetEntry());
        if (!p)
        {
            handler->SendSysMessage("No creature template for this entry.");
            return false;
        }

        uint32 const level = creature->GetLevel();
        float const liveHit = (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
        uint32 const liveAttackMs = creature->GetAttackTime(BASE_ATTACK);
        handler->PSendSysMessage("%s (entry %u) rank %u, level %u-%u (this one %u), class %u, damage x%.2f",
            p->name.c_str(), p->entry, p->rank, p->levelMin, p->levelMax, level, p->unitClass, p->damageMultiplier);
        handler->PSendSysMessage("  melee: profile %.0f per hit, %.1f dps at level %u | live %.0f per hit, %u ms, %.1f dps",
            CreatureProfiles::EstimateMeleeHit(*p, level), CreatureProfiles::EstimateMeleeDps(*p, level), level,
            liveHit, liveAttackMs, liveAttackMs ? liveHit / (liveAttackMs / 1000.0f) : 0.0f);
        handler->PSendSysMessage("  taunt immune: %s, mechanic immune mask 0x%X, flees at %.0f%% health",
            p->tauntImmune ? "yes" : "no", p->mechanicImmuneMask, p->fleeAtHealthPct);
        if (p->hasOverride)
            handler->PSendSysMessage("  override: interrupt x%.2f, taunt x%.2f  %s",
                p->override.interruptMult, p->override.tauntMult, p->override.comment.c_str());

        if (p->spells.empty())
            handler->SendSysMessage("  spells: none known (C++ script creatures are not covered)");
        for (CreatureSpellRef const& s : p->spells)
        {
            char head[160];
            if (s.source == SpellSource::OnHit)
                snprintf(head, sizeof(head), "  [%s via %u, %u%%%s] ", CreatureProfiles::SourceName(s.source),
                    s.viaAuraId, s.procChance, s.reapplies ? ", reapplies" : "");
            else
                snprintf(head, sizeof(head), "  [%s, target %s, repeat %u-%u ms%s] ", CreatureProfiles::SourceName(s.source),
                    TargetTypeName(s.castTarget), s.repeatMinMs, s.repeatMaxMs, s.reapplies ? ", reapplies" : "");
            ShowSpellLine(handler, s.spellId, level, liveHit, head);
        }
        return true;
    }

    bool ShowSpell(ChatHandler* handler, uint32 spellId, uint32 casterLevel)
    {
        if (!sSpellMgr.GetSpellEntry(spellId))
        {
            handler->PSendSysMessage("Unknown spell %u.", spellId);
            return false;
        }
        ShowSpellLine(handler, spellId, casterLevel, 0.0f, "");
        return true;
    }

    bool ShowRates(ChatHandler* handler, Player* player)
    {
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(player);
        if (!commander)
        {
            handler->SendSysMessage("No commander for your party on this map.");
            return false;
        }

        WorldModel const& world = commander->GetWorld();
        RateFacts const& r = world.rates;
        handler->PSendSysMessage("Party max health %.0f (1 point = %.0f health), %u healers, %u others",
            r.partyMaxHealth, r.partyMaxHealth / 100.0f, r.healers, r.damageDealers);
        handler->PSendSysMessage("Incoming melee %.1f/s = %.2f points/s; dealt %.1f/s (%s); enemy health %.0f, fight left %.1f s",
            r.incomingDps, Value::Points(world, r.incomingDps), Value::DealtDps(world),
            r.dealtMeasured ? "measured" : "estimate", r.enemyHealth, Value::FightRemainingSec(world));

        Map* map = player->GetMap();
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            Unit* unit = map->GetUnit(m.guid);
            handler->PSendSysMessage("  %s: %.2f points/s, global cooldown costs %.2f, death penalty %.0f",
                unit ? unit->GetName() : "?", Value::ContributionPerSec(world, m),
                Value::LostAction(world, m, Value::GCD_SEC), Value::DeathPenalty(m));
        }
        for (EnemyFacts const& e : world.enemies)
        {
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            Unit* unit = map->GetUnit(e.guid);
            Unit* victimUnit = victim ? map->GetUnit(victim->guid) : nullptr;
            if (victim)
                handler->PSendSysMessage("  enemy %s: %.1f melee dps, on %s %.1f after armor, %s survives %.1f s alone",
                    unit ? unit->GetName() : "?", e.meleeDps, victimUnit ? victimUnit->GetName() : "?",
                    Value::HitDps(e, *victim), victimUnit ? victimUnit->GetName() : "?", Value::SurviveSec(e, *victim));
            else
                handler->PSendSysMessage("  enemy %s: %.1f melee dps, not hitting us", unit ? unit->GetName() : "?", e.meleeDps);
        }
        return true;
    }

    void ReloadOverrides(ChatHandler* handler)
    {
        handler->SendSysMessage(sProfileStore.ReloadOverrides().c_str());
    }

    void ShowDecisions(ChatHandler* handler, BotDirectives const& directives, uint32 count)
    {
        auto const& log = directives.GetDecisionLog();
        if (log.empty())
        {
            handler->SendSysMessage("  no arbiter decisions recorded");
            return;
        }

        uint32 const now = WorldTimer::getMSTime();
        uint32 shown = 0;
        for (auto itr = log.rbegin(); itr != log.rend() && shown < count; ++itr, ++shown)
        {
            handler->PSendSysMessage("  decision %u ms ago:", WorldTimer::getMSTimeDiff(itr->timeMs, now));
            for (CandidateRecord const& c : itr->candidates)
            {
                if (c.valued)
                    handler->PSendSysMessage("    %s %s: tier %u, benefit %.2f - cost %.2f = %.2f -> %s %s",
                        c.source.c_str(), c.what.c_str(), c.tier, c.benefit, c.cost, c.score, ResultName(c.result), c.reason.c_str());
                else
                    handler->PSendSysMessage("    %s %s: tier %u, score %.2f -> %s %s",
                        c.source.c_str(), c.what.c_str(), c.tier, c.score, ResultName(c.result), c.reason.c_str());
            }
        }
    }
}
}
