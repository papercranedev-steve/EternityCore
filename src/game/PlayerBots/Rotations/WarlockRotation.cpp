/*
 * Warlock rotation (moved out of PartyBotAI.cpp 2026-11-14; reworked 2026-11-15 for the level 60
 * 22/3/26 spec with Siphon Life, Improved Imp and Ruin, user: no pet sacrifice for now).
 *
 * Out of combat: Detect Invisibility on the party, Demon Armor; with a victim (an opener): the
 * combat steps; without one: summon the pet (the Imp). The Imp stays in Phase Shift, in and
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
 * The Commander decides: Banish, Fear, Howl of Terror (crowd control), Rain of Fire (area
 * phase), Life Tap and Drain Life (HealthTradeBehavior: how
 * much health it spends depends on the healers), Drain Soul on enemies about to die
 * (HarvestBehavior: the Soul Shards).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Death Coil.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "Player.h"

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
                bot.SummonPetIfNeeded();
                KeepPetAside();
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
