/*
 * Warlock rotation (moved out of PartyBotAI.cpp 2026-11-14; reworked 2026-11-15 for the level 60
 * 22/3/26 spec with Siphon Life, Improved Imp and Ruin, user: no pet sacrifice for now).
 *
 * Out of combat: Detect Invisibility on the tanks and the real players (none in a raid
 * instance, user 2026-11-25), Demon Armor; with a victim (an opener): the
 * combat steps; without one: a Soulstone on the member the brain gives this warlock
 * (Commander/Coordination/Duties/SoulstoneAssigner.h, TeamPlan::soulstones; create it, use it).
 * The pet (the Imp, every ability its level allows) is summoned by PartyBotAI::UpdateAI out of
 * combat (SummonPetIfNeeded), not only here: resting and the walk back after a corpse run skip
 * the rotation, and the next pull came first. The Imp stays in Phase Shift, in and
 * out of combat (KeepPetAside, user 2026-11-15: no damage from it, so it does not die; its
 * Blood Pact stays).
 *
 * In combat:
 *   1. Shadowburn when it kills the victim (DamageSpells::EstimateDirect, no crit) and the
 *      warlock holds more than SHARD_RESERVE Soul Shards (a kill within 5 s gives it back).
 *   2. The best of Corruption, Siphon Life, Immolate, Conflagrate (ends the Immolate), Shadow
 *      Bolt (instant with Nightfall's Shadow Trance) and a wand shot on the victim:
 *      CastBestDamage, damage landed before it dies per busy second, less the mana
 *      (DamageSpellValue). Since 2026-11-27 (strat-20261127-000359: the periodic spells went
 *      first in a fixed order, on every new enemy, and no Shadow Bolt was cast in a whole
 *      fight; the wand came below 5% mana in any gap). The same while the wand runs
 *      (UpdateWhileAutoRepeating): a spell worth more ends it.
 *   3. Nothing castable on it: walk closer.
 * Gone 2026-11-26 (review): Death Coil on a victim in melee reach of the warlock or casting (it
 * is a 3 s fear: it ran the tank's target off the tank, past the doctrine's no-fear stamp).
 * Gone 2026-11-15: Searing Pain below 20% (a fire spell with high threat, worse than Shadow
 * Bolt here), Demonic Sacrifice, Life Tap below 10% mana and Drain Life below 30% health,
 * Curse of Agony (user: no curse until the curses are chosen for the party).
 *
 * The Commander decides: the damage curse (CurseAssigner picks Elements / Shadow / Recklessness
 * for the party, CurseBehavior casts it when CurseValue says it pays), Banish, Fear, Howl of
 * Terror, Death Coil (crowd control; Death Coil and Howl of Terror also on enemies hitting the
 * warlock, EscapeBehavior), Rain of Fire (area phase), Life Tap and Drain Life
 * (HealthTradeBehavior: tap and drain cycle when the mana runs out), Drain Soul on enemies
 * about to die (HarvestBehavior: the Soul Shards), whom the Soulstone is for (SoulstoneAssigner).
 */

#include "Shared/Phrases.h"
#include "Rotations/ClassRotation.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "DamageSpells.h"
#include "Player.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"

namespace
{
    // Shards kept for other uses (Soulstone, Healthstone, summoning); HarvestBehavior::KEEP
    // fills up to 10 (user 2026-11-15: "5-10").
    uint32 const SHARD_RESERVE = 5;
    // Minor Soulstone is trained at this level: below it, not knowing it is no news.
    uint32 const SOULSTONE_LEVEL = 18;

    // Why the member the Soulstone is for goes without (WarlockRotation::Note).
    enum class SoulstoneNote : uint8
    {
        None,
        NotLearned,
        NoShard,
        Cooldown,
    };

    class WarlockRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.warlock;

            if (s.pDetectInvisibility)
                if (Unit* pTarget = SelectDetectInvisibilityTarget(s.pDetectInvisibility))
                    if (Buff(pTarget, s.pDetectInvisibility))
                        return;

            if (Buff(me, s.pDemonArmor))
                return;

            EndBuffing(s.pDetectInvisibility);

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
            {
                // (the pet is summoned in PartyBotAI::UpdateAI, SummonPetIfNeeded)
                KeepPetAside();
                Soulstone();
                return;
            }
            SendPetAt(pVictim);
            UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.warlock;

            KeepPetAside();

            if (Unit* pVictim = me->GetVictim())
                Damage(pVictim);
        }

        // The wand runs (TeamBotAI skips UpdateInCombat then): the same choice, a spell worth
        // more than the next shot ends it (2026-11-27; before, only a Commander action did).
        void UpdateWhileAutoRepeating() override
        {
            if (!m_bot.me->IsInCombat())
                return;
            KeepPetAside();
            if (Unit* pVictim = m_bot.me->GetVictim())
                Damage(pVictim);
        }

    private:
        // Steps 1-3 (header).
        void Damage(Unit* pVictim)
        {
            auto const& s = m_bot.m_spells.warlock;
            if (KillsWithShadowburn(pVictim) && Cast(pVictim, s.pShadowburn))
                return;

            if (CastBestDamage(pVictim, { { s.pCorruption }, { s.pSiphonLife }, { s.pImmolate },
                                          { s.pConflagrate, s.pImmolate }, { s.pShadowBolt } }, true))
                return;

            ChaseWhenOutOfRange(pVictim);
        }

        // Detect Invisibility only on the tanks (Tanks::IsTank, bots and players) and the real
        // players; not in a raid instance (user 2026-11-25: on every member it wasted mana and
        // GCDs). Without a Commander (no party snapshot): nobody.
        Unit* SelectDetectInvisibilityTarget(SpellEntry const* pSpell) const
        {
            Player* me = m_bot.me;
            if (me->GetMap()->IsRaid())
                return nullptr;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            if (!commander)
                return nullptr;
            for (Commander::PartyMemberFacts const& m : commander->GetWorld().party)
            {
                if (!m.alive || m.isPet || (m.isBot && !Commander::Tanks::IsTank(m)))
                    continue;
                Unit* pTarget = me->GetMap()->GetUnit(m.guid);
                if (!pTarget || !pTarget->IsInWorld() || !me->IsValidHelpfulTarget(pTarget) ||
                    !m_bot.IsValidBuffTarget(pTarget, pSpell) ||
                    m_bot.IsBuffPendingFromOther(pTarget, pSpell, nullptr) ||
                    !me->IsWithinDist(pTarget, 30.0f) || !me->IsWithinLOSInMap(pTarget))
                    continue;
                return pTarget;
            }
            return nullptr;
        }

        // Out of combat, nothing else to do (user 2026-11-15, cooldown doc 2026-11-14-cooldown-spells.md
        // 10): the member the brain gives this warlock (SoulstoneAssigner; none without a
        // Commander) gets one. With the stone in the bags: use it on that member (its 30 min
        // cooldown is the warlock's); without: create the best one it knows (a Soul Shard,
        // SHARD_RESERVE is kept for this) while it and the leader stand still out of combat.
        // The member goes without one: the warlock says why, once per reason (user 2026-11-20);
        // not for itself (a lone warlock).
        bool Soulstone()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            if (me->IsInCombat())
                return false;
            Player* pTarget = PlannedTarget();
            if (!pTarget)
            {
                m_soulstoneNote = SoulstoneNote::None;
                return false;
            }
            SpellEntry const* pCreate = bot.m_spells.warlock.pCreateSoulstone;
            uint32 stone = 0;
            if (pCreate)
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (pCreate->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                        stone = uint32(pCreate->EffectItemType[i]);
            ItemPrototype const* pProto = stone ? sObjectMgr.GetItemPrototype(stone) : nullptr;
            SpellEntry const* pUse = pProto ? sSpellMgr.GetSpellEntry(pProto->Spells[0].SpellId) : nullptr;
            // (the plan is from the last refresh: a stone may have landed since)
            if (pUse && pTarget->HasAura(pUse->Id))
                return false;
            if (!pCreate || !pUse)
            {
                if (me->GetLevel() >= SOULSTONE_LEVEL)
                    Note(SoulstoneNote::NotLearned, pTarget);
                return false;
            }
            if (!me->IsSpellReady(pUse, pProto))
            {
                Note(SoulstoneNote::Cooldown, pTarget, me->GetSpellCooldownLeft(pUse));
                return false;
            }
            if (me->HasItemCount(stone, 1))
            {
                m_soulstoneNote = SoulstoneNote::None;
                return me->IsWithinLOSInMap(pTarget) && pUse->IsTargetInRange(me, pTarget) &&
                       bot.UseBagItem(stone, pTarget);
            }
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (pCreate->Reagent[i] > 0 && !me->HasItemCount(uint32(pCreate->Reagent[i]), pCreate->ReagentCount[i]))
                {
                    Note(SoulstoneNote::NoShard, pTarget);
                    return false;
                }
            if (me->IsMoving())
                return false;
            if (Player* pLeader = bot.GetPartyLeader())
                if (pLeader != me && (pLeader->IsMoving() || pLeader->IsInCombat()))
                    return false;
            return Cast(me, pCreate);
        }

        // Why the member has no Soulstone, in party chat (the party voice). Said again only for
        // another reason; nothing to say when the stone was for itself.
        void Note(SoulstoneNote note, Player const* pMember, int32 cooldownMs = 0)
        {
            if (note == m_soulstoneNote)
                return;
            m_soulstoneNote = note;
            if (pMember == m_bot.me)
                return;
            BotShared::Phrases::Vars vars;
            vars["name"] = pMember->GetName();
            char const* key = nullptr;
            switch (note)
            {
                case SoulstoneNote::NotLearned:
                    key = "soulstone.unlearned";
                    break;
                case SoulstoneNote::NoShard:
                    key = "soulstone.noshard";
                    break;
                case SoulstoneNote::Cooldown:
                    key = cooldownMs > 0 ? "soulstone.cooldown" : "soulstone.cooldown.unknown";
                    vars["minutes"] = std::to_string((cooldownMs + MINUTE * IN_MILLISECONDS - 1) / (MINUTE * IN_MILLISECONDS));
                    break;
                case SoulstoneNote::None:
                    return;
            }
            Commander::VoiceEvent e;
            e.key = key;
            e.speaker = m_bot.me->GetObjectGuid();
            e.vars.assign(vars.begin(), vars.end());
            e.tier = Commander::VoiceTier::Essential;
            e.onceMs = 10 * IN_MILLISECONDS;    // (said again only for another reason: m_soulstoneNote)
            e.once = std::string(key) + ":" + pMember->GetName();
            sCommanderMgr.QueueVoice(m_bot.me, std::move(e));
        }

        // The member TeamPlan::soulstones gives this warlock, alive on its map; null = none.
        Player* PlannedTarget() const
        {
            Player* me = m_bot.me;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            if (!commander)
                return nullptr;
            ObjectGuid const guid = commander->GetPlan().soulstones.TargetFor(me->GetObjectGuid());
            Player* pTarget = guid.IsEmpty() ? nullptr : me->GetMap()->GetPlayer(guid);
            return pTarget && pTarget->IsInWorld() && pTarget->IsAlive() ? pTarget : nullptr;
        }

        // Step 2: spare shards, and its average direct damage takes the victim's whole health.
        bool KillsWithShadowburn(Unit* pVictim) const
        {
            Player* me = m_bot.me;
            SpellEntry const* pSpell = m_bot.m_spells.warlock.pShadowburn;
            if (!pSpell)
                return false;
            if (pSpell->Reagent[0] && me->GetItemCount(pSpell->Reagent[0]) <= SHARD_RESERVE)
                return false;
            return DamageSpells::EstimateDirect(me, pVictim, pSpell) >= float(pVictim->GetHealth());
        }

        // The last reason said (Note); None once the member has a stone or nobody is planned.
        SoulstoneNote m_soulstoneNote = SoulstoneNote::None;
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarlock(TeamBotAI& bot)
{
    return std::make_unique<WarlockRotation>(bot);
}
