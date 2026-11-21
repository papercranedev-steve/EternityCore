/*
 * Warlock rotation (moved out of PartyBotAI.cpp 2026-11-14; reworked 2026-11-15 for the level 60
 * 22/3/26 spec with Siphon Life, Improved Imp and Ruin, user: no pet sacrifice for now).
 *
 * Out of combat: Detect Invisibility on the party, Demon Armor; with a victim (an opener): the
 * combat steps; without one: a Soulstone on the party's healer (Soulstone: create it, use it).
 * The pet (the Imp, every ability its level allows) is summoned by PartyBotAI::UpdateAI out of
 * combat (SummonPetIfNeeded), not only here: resting and the walk back after a corpse run skip
 * the rotation, and the next pull came first. The Imp stays in Phase Shift, in and
 * out of combat (KeepPetAside, user 2026-11-15: no damage from it, so it does not die; its
 * Blood Pact stays).
 *
 * In combat:
 *   1. Death Coil when the victim is in melee reach of the warlock or casting.
 *   2. Shadowburn when it kills the victim (DamageSpells::EstimateDirect, no crit) and the
 *      warlock holds more than SHARD_RESERVE Soul Shards (a kill within 5 s gives it back).
 *   3. Corruption, Siphon Life, Immolate, Conflagrate. A periodic spell on an enemy dying
 *      before it pays is refused for every cast (Constraints::WastedCast).
 *   4. Walk closer, Shadow Bolt (instant with Nightfall's Shadow Trance), wand below 5% mana.
 * Gone 2026-11-15: Searing Pain below 20% (a fire spell with high threat, worse than Shadow
 * Bolt here), Demonic Sacrifice, Life Tap below 10% mana and Drain Life below 30% health,
 * Curse of Agony (user: no curse until the curses are chosen for the party).
 *
 * The Commander decides: the damage curse (CurseAssigner picks Elements / Shadow / Recklessness
 * for the party, CurseBehavior casts it when CurseValue says it pays), Banish, Fear, Howl of Terror (crowd control), Rain of Fire (area
 * phase), Life Tap and Drain Life (HealthTradeBehavior: tap
 * and drain cycle when the mana runs out), Drain Soul on enemies about to die
 * (HarvestBehavior: the Soul Shards).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Death Coil.
 */

#include "Shared/Phrases.h"
#include "Rotations/ClassRotation.h"
#include "Commander/Commands/BotSpeech.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "DamageSpells.h"
#include "RoleCapability.h"
#include "Player.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"

namespace
{
    float const WAND_MANA_PCT = 5.0f;
    // Shards kept for other uses (Soulstone, Healthstone, summoning); HarvestBehavior::KEEP
    // fills up to 10 (user 2026-11-15: "5-10").
    uint32 const SHARD_RESERVE = 5;
    // Minor Soulstone is trained at this level: below it, not knowing it is no news.
    uint32 const SOULSTONE_LEVEL = 18;

    // Why the party's healer goes without a Soulstone (WarlockRotation::Note).
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
                if (Player* pTarget = bot.SelectBuffTarget(s.pDetectInvisibility))
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

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if ((pVictim->CanReachWithMeleeAutoAttack(me) || pVictim->IsNonMeleeSpellCasted()) &&
                Cast(pVictim, s.pDeathCoil))
                return;
            if (KillsWithShadowburn(pVictim) && Cast(pVictim, s.pShadowburn))
                return;

            if (Cast(pVictim, s.pCorruption))
                return;
            if (Cast(pVictim, s.pSiphonLife))
                return;
            if (Cast(pVictim, s.pImmolate))
                return;
            if (Cast(pVictim, s.pConflagrate))
                return;

            ChaseWhenOutOfRange(pVictim);

            if (Cast(pVictim, s.pShadowBolt))
                return;

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }

    private:
        // Out of combat, nothing else to do (user 2026-11-15, cooldown doc 2026-11-14-cooldown-spells.md
        // 10): the party's healer without a Soulstone Resurrection gets one. With the stone in the
        // bags: use it on the healer (its 30 min cooldown is the warlock's); without: create the
        // best one it knows (a Soul Shard, SHARD_RESERVE is kept for this) while it and the
        // leader stand still out of combat.
        // The healer goes without one: the warlock says why, once per reason (user 2026-11-20).
        bool Soulstone()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            if (me->IsInCombat())
                return false;
            SpellEntry const* pCreate = bot.m_spells.warlock.pCreateSoulstone;
            uint32 stone = 0;
            if (pCreate)
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (pCreate->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                        stone = uint32(pCreate->EffectItemType[i]);
            ItemPrototype const* pProto = stone ? sObjectMgr.GetItemPrototype(stone) : nullptr;
            SpellEntry const* pUse = pProto ? sSpellMgr.GetSpellEntry(pProto->Spells[0].SpellId) : nullptr;
            Player* pHealer = SoulstoneTarget(pUse);
            if (!pHealer)
            {
                m_soulstoneNote = SoulstoneNote::None;
                return false;
            }
            if (!pCreate || !pUse)
            {
                if (me->GetLevel() >= SOULSTONE_LEVEL)
                    Note(SoulstoneNote::NotLearned, pHealer);
                return false;
            }
            if (!me->IsSpellReady(pUse, pProto))
            {
                Note(SoulstoneNote::Cooldown, pHealer, me->GetSpellCooldownLeft(pUse));
                return false;
            }
            if (me->HasItemCount(stone, 1))
            {
                m_soulstoneNote = SoulstoneNote::None;
                return me->IsWithinLOSInMap(pHealer) && pUse->IsTargetInRange(me, pHealer) &&
                       bot.UseBagItem(stone, pHealer);
            }
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (pCreate->Reagent[i] > 0 && !me->HasItemCount(uint32(pCreate->Reagent[i]), pCreate->ReagentCount[i]))
                {
                    Note(SoulstoneNote::NoShard, pHealer);
                    return false;
                }
            if (me->IsMoving())
                return false;
            if (Player* pLeader = bot.GetPartyLeader())
                if (pLeader != me && (pLeader->IsMoving() || pLeader->IsInCombat()))
                    return false;
            return Cast(me, pCreate);
        }

        // Why the healer has no Soulstone, in party chat (BotSpeech::Report; speech off: a
        // system message to the leader). Said again only for another reason.
        void Note(SoulstoneNote note, Player const* pHealer, int32 cooldownMs = 0)
        {
            if (note == m_soulstoneNote)
                return;
            m_soulstoneNote = note;
            BotShared::Phrases::Vars vars;
            vars["name"] = pHealer->GetName();
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
            std::string const text = BotShared::Phrases::Text(BotShared::I18n::OfParty(m_bot.me), key, vars);
            Commander::BotSpeech::Report(m_bot.me, m_bot.GetPartyLeader(), text);
        }

        // A healer of the party (a healer bot; a player whose best role is healing,
        // RoleCapabilities) without a Soulstone Resurrection (any rank: the stone's on-use aura
        // or another warlock's).
        Player* SoulstoneTarget(SpellEntry const* pUse) const
        {
            Player* me = m_bot.me;
            Player* pPlayerHealer = nullptr;
            for (Player* pMember : Commander::Team::Members(me))
            {
                if (!pMember || !pMember->IsAlive() || pMember->GetMap() != me->GetMap() || HasSoulstone(pMember, pUse))
                    continue;
                if (TeamBotAI const* pAI = pMember->AI() ? dynamic_cast<TeamBotAI const*>(pMember->AI()) : nullptr)
                {
                    if (pAI->GetRole() == ROLE_HEALER)
                        return pMember;
                }
                else if (!pPlayerHealer && RoleCapabilities::Assess(pMember).Best() == ROLE_HEALER)
                    pPlayerHealer = pMember;
            }
            return pPlayerHealer;
        }

        static bool HasSoulstone(Player const* pMember, SpellEntry const* pUse)
        {
            // Soulstone Resurrection, ranks 1-5 (Player::SelectResurrectionSpellId).
            static uint32 const RANKS[] = { 20707, 20762, 20763, 20764, 20765 };
            if (pUse && pMember->HasAura(pUse->Id))
                return true;
            for (uint32 id : RANKS)
                if (pMember->HasAura(id))
                    return true;
            return false;
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

        // The last reason said (Note); None once the healer has a stone or there is no healer.
        SoulstoneNote m_soulstoneNote = SoulstoneNote::None;
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarlock(TeamBotAI& bot)
{
    return std::make_unique<WarlockRotation>(bot);
}
