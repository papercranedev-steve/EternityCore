#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/CastValue.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Spell.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        struct LiveCast
        {
            SpellEntry const* spell = nullptr;
            ObjectGuid target;
            float progress = 0.0f;
            uint32 remainingMs = 0;
        };

        // Live check on the map thread: the plan can be up to one perception interval old.
        bool IsCastingInterruptible(Unit const* enemy, LiveCast& out)
        {
            Spell const* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            bool channeled = false;
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
            {
                spell = enemy->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                channeled = true;
            }
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
                return false;
            if (!SpellTags::IsInterruptible(spell->m_spellInfo, channeled, spell->GetCastTime()))
                return false;

            int32 const total = channeled ? spell->m_spellInfo->GetDuration() : spell->GetCastTime();
            out.progress = total > 0 ? 1.0f - float(spell->GetCastedTime()) / float(total) : 0.0f;
            out.remainingMs = uint32(spell->GetCastedTime());     // m_timer counts down to 0
            out.spell = spell->m_spellInfo;
            out.target = spell->m_targets.getUnitTargetGuid();
            return true;
        }

        // Benefit of stopping the live cast, using the perceived facts of the enemy.
        CastValue LiveBenefit(WorldModel const& world, EnemyFacts const& facts, LiveCast const& cast)
        {
            EnemyFacts e = facts;
            e.castingSpellId = cast.spell->Id;
            e.castTarget = cast.target;
            e.castInterruptible = true;
            e.castRemainingMs = cast.remainingMs;
            return CastValues::InterruptBenefit(world, e);
        }

        bool HasReadyInterrupt(PartyMemberFacts const* m)
        {
            if (!m || !m->alive || m->casting)
                return false;
            return std::any_of(m->interrupts.begin(), m->interrupts.end(),
                [](InterruptAbility const& a) { return a.ready; });
        }
    }

    bool InterruptBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        switch (bot->me->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_MAGE:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
            case CLASS_PALADIN:
                return true;
            default:
                return false;
        }
    }

    void InterruptBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return;

        std::vector<Proposal> made;
        for (InterruptDuty const& duty : plan.interrupts)
        {
            bool const isPrimary = duty.primary == self;
            bool const isBackup = duty.backup == self;
            if (!isPrimary && !isBackup)
                continue;

            Unit* enemy = me->GetMap()->GetUnit(duty.enemy);
            EnemyFacts const* facts = world.FindEnemy(duty.enemy);
            if (!enemy || !enemy->IsAlive() || !facts)
                continue;

            LiveCast cast;
            if (!IsCastingInterruptible(enemy, cast))
                continue;

            if (!isPrimary)
            {
                bool const primaryUnable = duty.primary.IsEmpty() || !HasReadyInterrupt(world.FindMember(duty.primary));
                if (!primaryUnable && cast.progress < BACKUP_AFTER_PROGRESS)
                    continue;
            }

            CastValue const value = LiveBenefit(world, *facts, cast);
            ProposalTier const tier = value.lethal ? ProposalTier::Urgent : ProposalTier::Assigned;

            // Every ready ability, assigned one first, then cheaper cooldowns: the arbiter tries
            // them in order of benefit - cost if one fails (out of range, immune, wrong form).
            std::vector<InterruptAbility> abilities = myFacts->interrupts;
            uint32 const assigned = isPrimary ? duty.primarySpellId : duty.backupSpellId;
            std::stable_sort(abilities.begin(), abilities.end(), [assigned](InterruptAbility const& a, InterruptAbility const& b)
            {
                if ((a.spellId == assigned) != (b.spellId == assigned))
                    return a.spellId == assigned;
                return a.cooldownMs < b.cooldownMs;
            });

            float preference = 0.0f;    // tie-break inside equal values: assigned first
            bool anyMelee = false;
            for (InterruptAbility const& a : abilities)
            {
                if (!a.ready)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                if (!spell)
                    continue;
                anyMelee = anyMelee || a.maxRange <= MELEE_ABILITY_RANGE;

                Proposal p;
                p.tier = tier;
                p.source = "interrupt";
                p.reason = isPrimary ? "primary" : "backup";
                p.action.type = BotActionType::Cast;
                p.action.target = duty.enemy;
                p.action.spell = spell;
                p.SetValue(value.points, CastValues::InterruptCost(world, *myFacts, a, *facts, value.points) + preference);
                made.push_back(p);
                preference += 0.01f;
            }

            // Melee primary not in reach: go to the caster (original chase logic moves us).
            if (isPrimary && anyMelee && bot->GetRole() != ROLE_TANK && me->GetVictim() != enemy &&
                !me->CanReachWithMeleeAutoAttack(enemy))
            {
                InterruptAbility const* melee = nullptr;
                for (InterruptAbility const& a : abilities)
                    if (a.ready && a.maxRange <= MELEE_ABILITY_RANGE && (!melee || a.cooldownMs < melee->cooldownMs))
                        melee = &a;
                Proposal p;
                p.tier = tier;
                p.source = "interrupt";
                p.reason = "go to caster";
                p.action.type = BotActionType::Attack;
                p.action.target = duty.enemy;
                // Walking in is part of InterruptCost; a little extra so a ranged option wins ties.
                p.SetValue(value.points, (melee ? CastValues::InterruptCost(world, *myFacts, *melee, *facts, value.points) : value.points) + 0.05f);
                made.push_back(p);
            }
        }

        ProposeOwnTargetVeto(bot, world, made, out);
        out.insert(out.end(), made.begin(), made.end());
    }

    void InterruptBehavior::ProposeOwnTargetVeto(PartyBotAI const* bot, WorldModel const& world,
                                                 std::vector<Proposal> const& made, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit* victim = me->GetVictim();
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!victim || !myFacts || myFacts->interrupts.empty())
            return;
        EnemyFacts const* facts = world.FindEnemy(victim->GetObjectGuid());
        LiveCast cast;
        if (!facts || !IsCastingInterruptible(victim, cast))
            return;

        // A worthwhile duty interrupt on the same enemy: let that one run.
        for (Proposal const& p : made)
            if (p.action.target == victim->GetObjectGuid() && p.benefit > p.cost)
                return;

        CastValue const value = LiveBenefit(world, *facts, cast);
        float cheapest = -1.0f;
        for (InterruptAbility const& a : myFacts->interrupts)
        {
            float const c = CastValues::InterruptCost(world, *myFacts, a, *facts, value.points);
            if (cheapest < 0.0f || c < cheapest)
                cheapest = c;
        }

        Proposal p;
        p.kind = ProposalKind::Veto;
        p.source = "interrupt";
        p.reason = "cast not worth an interrupt";
        p.veto.type = VetoType::Interrupt;
        p.veto.target = victim->GetObjectGuid();
        // Benefit of the veto = the interrupt we keep; cost = what the cast does.
        p.SetValue(std::max(0.0f, cheapest), value.points);
        out.push_back(p);
    }
}
