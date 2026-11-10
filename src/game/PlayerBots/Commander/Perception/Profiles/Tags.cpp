#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/SpawnSources.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <vector>

namespace Commander
{
namespace Tags
{
    Engage EngageOfEntry(uint32 entry)
    {
        if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry))
            if (profile->mainRangedSpellId)
                return Engage::StandBack;
        return Engage::Melee;
    }

    Engage EngageOf(EnemyFacts const& e)
    {
        return e.meleeEnabled ? EngageOfEntry(e.entry) : Engage::StandBack;
    }

    float MedianHealth(std::vector<uint32> health)
    {
        if (health.empty())
            return 0.0f;
        std::sort(health.begin(), health.end());
        return float(health[health.size() / 2]);
    }

    float EliteMedianHealth(WorldModel const& world)
    {
        std::vector<uint32> health;
        for (EnemyFacts const& e : world.enemies)
            if (e.elite && !e.onAllyOnly && e.maxHealth)
                health.push_back(e.maxHealth);
        return MedianHealth(std::move(health));
    }

    Tier TierOf(WorldModel const& world, bool elite, uint32 maxHealth, float eliteMedianHealth)
    {
        if (elite)
        {
            if (eliteMedianHealth > 0.0f && float(maxHealth) >= HEAVY_HEALTH_MULT * eliteMedianHealth)
                return Tier::Heavy;
            return Tier::Elite;
        }
        float const dps = Value::DealtDps(world);
        if (dps > 0.0f && float(maxHealth) / dps < FODDER_TTK_SEC)
            return Tier::Fodder;
        return Tier::Normal;
    }

    Tier TierOf(WorldModel const& world, EnemyFacts const& e, float eliteMedianHealth)
    {
        return TierOf(world, e.elite, e.maxHealth, eliteMedianHealth);
    }

    TeamLedger LedgerOf(WorldModel const& world)
    {
        TeamLedger l;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.CanAct())
                continue;
            if (!m.areaSpells.empty())
            {
                float cheapest = m.areaSpells.front().mana;
                for (AreaSpellFacts const& a : m.areaSpells)
                    cheapest = std::min(cheapest, a.mana);
                if (m.areaChannel.spellId || float(m.mana) >= cheapest)
                    ++l.areaDealers;
            }
            for (ControlAbility const& a : m.controls)
                if (a.Kind() == ControlKind::Mez && !a.peacefulOnly)
                    l.mezTypes |= a.creatureTypeMask ? a.creatureTypeMask : ~0u;
        }
        return l;
    }

    Source SourceOf(EnemyFacts const& e)
    {
        uint8 const s = SpawnSources::Of(e.entry);
        if (s & SpawnSources::FROM_TRAP)
            return Source::Trap;
        if (s & SpawnSources::FROM_DEATH)
            return Source::DeathSpawn;
        if (s & SpawnSources::FROM_AGGRO)
            return Source::Called;
        return e.summoned ? Source::Summoned : Source::Static;
    }

    uint16 RolesOf(uint32 entry)
    {
        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(entry);
        if (!p)
            return 0;
        uint16 roles = 0;
        if (p->selfDestruct.spellId)
            roles |= TAG_SELF_DESTRUCT;
        if (!p->aggroSpawns.empty())
            roles |= TAG_ALARM;
        if (!p->summonTimers.empty())
            roles |= TAG_SUMMONER;
        if (p->fleeAtHealthPct > 0.0f)
            roles |= TAG_FLEES;
        if (p->tauntImmune || (p->hasOverride && p->override.tauntMult <= 0.0f))
            roles |= TAG_TAUNT_IMMUNE;
        if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry))
            if (info->type == CREATURE_TYPE_TOTEM)
                roles |= TAG_TOTEM;
        for (CreatureSpellRef const& ref : p->spells)
        {
            if (ref.source == SpellSource::OnHit)
                continue;
            if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(ref.spellId))
                for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_INSTAKILL && spell->EffectImplicitTargetA[i] == TARGET_UNIT_CASTER)
                        roles |= TAG_SELF_DESTRUCT;
            std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(ref.spellId);
            if (!sp)
                continue;
            switch (sp->category)
            {
                case CastCategory::Heal:      roles |= TAG_HEALER; break;
                case CastCategory::Summon:    roles |= TAG_SUMMONER; break;
                case CastCategory::ManaDrain: roles |= TAG_MANA_DRAIN; break;
                default: break;
            }
        }
        return roles;
    }

    std::string RoleNames(uint16 roles)
    {
        static struct { uint16 bit; char const* name; } const names[] = {
            { TAG_SELF_DESTRUCT, "self-destructs" }, { TAG_ALARM, "alarm" }, { TAG_HEALER, "healer" },
            { TAG_SUMMONER, "summoner" }, { TAG_MANA_DRAIN, "mana drain" }, { TAG_TOTEM, "totem" },
            { TAG_FLEES, "flees" }, { TAG_TAUNT_IMMUNE, "taunt immune" } };
        std::string out;
        for (auto const& n : names)
            if (roles & n.bit)
                out += (out.empty() ? "" : ", ") + std::string(n.name);
        return out;
    }

    BlastFacts BlastOf(EnemyFacts const& e)
    {
        BlastFacts b;
        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(e.entry);
        if (!p || !p->selfDestruct.spellId)
            return b;
        SelfDestruct const& sd = p->selfDestruct;
        b.valid = true;
        b.radius = sd.radius;
        b.damage = sd.damage;
        b.physical = sd.physical;
        if (sd.afterMaxMs && e.firstSeenMs)
        {
            b.earliestMs = e.firstSeenMs + sd.afterMinMs;
            b.latestMs = e.firstSeenMs + sd.afterMaxMs;
        }
        return b;
    }

    char const* Name(Source source)
    {
        switch (source)
        {
            case Source::Static:     return "static";
            case Source::Summoned:   return "summoned";
            case Source::Trap:       return "trap";
            case Source::DeathSpawn: return "death spawn";
            case Source::Called:     return "called";
        }
        return "?";
    }

    char const* Name(Engage engage)
    {
        switch (engage)
        {
            case Engage::Melee:     return "melee";
            case Engage::StandBack: return "stands back";
        }
        return "?";
    }

    char const* Name(Tier tier)
    {
        switch (tier)
        {
            case Tier::Fodder: return "fodder";
            case Tier::Normal: return "normal";
            case Tier::Elite:  return "elite";
            case Tier::Heavy:  return "heavy";
        }
        return "?";
    }
}
}
