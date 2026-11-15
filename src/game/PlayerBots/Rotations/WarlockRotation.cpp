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
 * phase), Life Tap and Drain Life (HealthTradeBehavior: how
 * much health it spends depends on the healers), Drain Soul on enemies about to die
 * (HarvestBehavior: the Soul Shards).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Death Coil.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
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

    class WarlockRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
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
            PartyBotAI& bot = m_bot;
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
        bool Soulstone()
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            SpellEntry const* pCreate = bot.m_spells.warlock.pCreateSoulstone;
            if (!pCreate || me->IsInCombat())
                return false;
            uint32 stone = 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (pCreate->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                    stone = uint32(pCreate->EffectItemType[i]);
            ItemPrototype const* pProto = stone ? sObjectMgr.GetItemPrototype(stone) : nullptr;
            SpellEntry const* pUse = pProto ? sSpellMgr.GetSpellEntry(pProto->Spells[0].SpellId) : nullptr;
            if (!pUse || !me->IsSpellReady(pUse, pProto))
                return false;
            Player* pHealer = SoulstoneTarget(pUse);
            if (!pHealer)
                return false;
            if (me->HasItemCount(stone, 1))
                return me->IsWithinLOSInMap(pHealer) && pUse->IsTargetInRange(me, pHealer) &&
                       bot.UseBagItem(stone, pHealer);
            if (me->IsMoving())
                return false;
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (pCreate->Reagent[i] > 0 && !me->HasItemCount(uint32(pCreate->Reagent[i]), pCreate->ReagentCount[i]))
                    return false;
            if (Player* pLeader = bot.GetPartyLeader())
                if (pLeader != me && (pLeader->IsMoving() || pLeader->IsInCombat()))
                    return false;
            return Cast(me, pCreate);
        }

        // A healer of the party (a healer bot; a player whose best role is healing,
        // RoleCapabilities) without a Soulstone Resurrection (any rank: the stone's on-use aura
        // or another warlock's).
        Player* SoulstoneTarget(SpellEntry const* pUse) const
        {
            Player* me = m_bot.me;
            Group* pGroup = me->GetGroup();
            if (!pGroup)
                return nullptr;
            Player* pPlayerHealer = nullptr;
            for (GroupReference* itr = pGroup->GetFirstMember(); itr; itr = itr->next())
            {
                Player* pMember = itr->getSource();
                if (!pMember || !pMember->IsAlive() || pMember->GetMap() != me->GetMap() || HasSoulstone(pMember, pUse))
                    continue;
                if (PartyBotAI const* pAI = pMember->AI() ? dynamic_cast<PartyBotAI const*>(pMember->AI()) : nullptr)
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
            if (pMember->HasAura(pUse->Id))
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
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarlock(PartyBotAI& bot)
{
    return std::make_unique<WarlockRotation>(bot);
}
