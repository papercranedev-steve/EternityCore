/*
 * Priest rotation (moved out of PartyBotAI.cpp 2026-11-14).
 *
 * Out of combat: Fortitude (Inner Focus first when it is Prayer of Fortitude, so it costs no
 * mana), Divine Spirit, Shadow Protection (single or prayer), Inner Fire, the heal step
 * (heals the team: Commander RestTopUp); with a victim (an opener): the combat steps.
 *
 * In combat:
 *   1. Heals the team (a healer, or nobody else heals): the heal step (the Commander heal
 *      module's order, none = rest), then its damage step (a healer: its tier, HealerDamage; a
 *      side healer that also deals damage for the team: Damage).
 *   2. Others: Damage: Shadowform, Vampiric Embrace, Mind Blast, Shadow Word: Pain, Devouring
 *      Plague, Mind Flay (nothing within 10 yd or shielded), walk closer, Smite (caster form),
 *      wand below 10% mana.
 *
 * Shadowform and heals: a shadow priest that heals for the team fights in Shadowform; a heal
 * the heal module orders leaves the form first (PriestKit::LeavesFormFor, TeamBotAI::
 * DoCastWithStance), and the form comes back at the next fight, not after each heal
 * (TeamBotAI::LeftFormThisFight). Until 2026-11-26 it left the form at the start of every fight
 * once it had the heal function (a lone shadow priest, one next to a warrior) and never took
 * it again.
 *
 * The Commander decides: Power Word: Shield (heal module: hit while casting; team rescue: about
 * to die), Inner Focus (heal boost), Fade, Desperate Prayer (heal module, team rescue), Shackle
 * Undead and Psychic Scream (crowd control), Silence (InterruptBehavior), Holy Nova (area
 * phase), Power Infusion (BoostBehavior), dispels in combat (DispelBehavior), every heal.
 * Removed 2026-11-26 (decisions the rotation made on its own): the shield on itself in heal
 * danger or below 50% health, Inner Focus below 50% mana, shields on allies about to die (also
 * from a damage priest without a victim), the pre-heal, a self heal below 40% when alone, and
 * Mana Burn on any mana user (little damage for its mana).
 * Devouring Plague stays on cooldown: a short-lived victim is skipped by the wasted-cast check
 * (TeamBotAI::IsCastWasted, periodic damage that does not pay).
 */

#include "Rotations/ClassRotation.h"
#include "Commander/Coordination/TeamPlan.h"
#include "TeamBotAI.h"
#include "Player.h"

namespace
{
    float const WAND_MANA_PCT = 10.0f;

    class PriestRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.priest;

            if (PartyBuff(s.pPowerWordFortitude, s.pPrayerofFortitude, s.pInnerFocus) ||
                PartyBuff(s.pDivineSpirit, s.pPrayerofSpirit) ||
                PartyBuff(s.pShadowProtection, s.pPrayerofShadowProtection) ||
                Buff(me, s.pInnerFire))
                return;

            EndBuffing(s.pPowerWordFortitude);

            if (bot.HealsForTeam() && bot.FindAndHealInjuredAlly())
                return;

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Unit* pVictim = bot.me->GetVictim();

            if (bot.HealsForTeam())
            {
                if (bot.FindAndHealInjuredAlly())
                    return;
                if (!pVictim)
                    return;
                if (bot.GetRole() == ROLE_HEALER)
                    HealerDamage(pVictim, bot.HealerDamageNow());
                else if (bot.DealsDamageForTeam())
                    Damage(pVictim);
                return;
            }

            if (pVictim)
                Damage(pVictim);
        }

    private:
        // pFreeCast (Inner Focus): cast first when the group version is chosen, so the prayer
        // costs no mana (user, 2026-11-15). The prayer follows on the next update.
        bool PartyBuff(SpellEntry const* pSingle, SpellEntry const* pGroup, SpellEntry const* pFreeCast = nullptr)
        {
            SpellEntry const* pBuffSpell = nullptr;
            auto* pTarget = m_bot.SelectBuffTarget(pSingle, pGroup, pBuffSpell);
            if (!pTarget)
                return false;

            Player* me = m_bot.me;
            if (pFreeCast && pBuffSpell == pGroup && !me->HasAura(pFreeCast->Id) && Cast(me, pFreeCast))
                return true;

            return Buff(pTarget, pBuffSpell);
        }

        // A healer's damage step by its tier this tick (healer damage tiers doc 2026-11-21,
        // section 6): FULL Shadow Word: Pain, Mind Blast, Smite; SPARE Shadow Word: Pain, Smite;
        // then (FREE and up) the wand. No Shadowform (holy spells).
        void HealerDamage(Unit* pVictim, uint8 tier)
        {
            if (tier == Commander::HEALER_DAMAGE_NONE)
                return;
            TeamBotAI& bot = m_bot;
            auto const& s = bot.m_spells.priest;
            if (tier >= Commander::HEALER_DAMAGE_SPARE)
            {
                if (Cast(pVictim, s.pShadowWordPain))
                    return;
                if (tier >= Commander::HEALER_DAMAGE_FULL && Cast(pVictim, s.pMindBlast))
                    return;
                ChaseWhenOutOfRange(pVictim);
                if (bot.me->GetShapeshiftForm() == FORM_NONE && Cast(pVictim, s.pSmite))
                    return;
            }
            ShootWand(pVictim);
        }

        void Damage(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.priest;

            // Not again in a fight it left the form in for a heal (header).
            if (!bot.LeftFormThisFight() && Cast(me, s.pShadowform))
                return;
            if (Cast(pVictim, s.pVampiricEmbrace))
                return;
            if (Cast(pVictim, s.pMindBlast))
                return;
            if (Cast(pVictim, s.pShadowWordPain))
                return;
            if (Cast(pVictim, s.pDevouringPlague))
                return;
            if ((!bot.GetAttackersInRangeCount(10.0f) || me->HasAuraType(SPELL_AURA_SCHOOL_ABSORB)) &&
                Cast(pVictim, s.pMindFlay))
                return;

            ChaseWhenOutOfRange(pVictim);

            if (me->GetShapeshiftForm() == FORM_NONE && Cast(pVictim, s.pSmite))
                return;

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakePriest(TeamBotAI& bot)
{
    return std::make_unique<PriestRotation>(bot);
}
