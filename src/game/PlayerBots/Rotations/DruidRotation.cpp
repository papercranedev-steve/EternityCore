/*
 * Druid rotation (moved out of PartyBotAI.cpp 2026-11-14). One class, three forms: the role
 * picks the form (cat: melee damage, bear: tank or melee without cat, moonkin: ranged damage),
 * the form picks the steps.
 *
 * Out of combat: healer leaves its form; Mark / Gift of the Wild, Thorns (only tanks, hunter
 * pets and taunting pets, user 2026-11-15), Nature's Grasp;
 * caster form: into the combat form, heals above 80% mana; cat: a Prowl no module wants is
 * cancelled (ClassRotation::DropUnwantedStealth; the hunt's opener prowls, HuntBehavior); with
 * a victim (an opener): the combat steps.
 *
 * In combat:
 *   Caster form: heals (the Commander heal module while the party fights), then into the
 *   combat form. A tank or melee druid keeps its form's mana while out of it
 *   (TeamBotAI::IsFormManaReserved). Rooted: it stays in its form (the upstream PartyBot left
 *   the form to break roots: rage gone, 55% base mana for Dire Bear Form each time; removed
 *   2026-11-26 after strat-20261126-000902, where four webs left the tank in caster form).
 *   Cat: stealthed Pounce / Ravage / Tiger's Fury; 5 combo points Ferocious Bite, Rip; out of
 *   reach Faerie Fire (Feral); Shred, Rake, Claw. (Dash: the flee interception, to catch a
 *   runner, user 2026-11-15; until then on any moving victim out of reach.)
 *   Bear tank: back to bear before any heal; Enrage below 20 rage (above 60% health, attacked),
 *   Faerie Fire (Feral), Maul (with 2+ enemies at the victim only when Swipe's rage is left
 *   too, so the threat module's Swipe is not starved by the next swing).
 *   Bear damage: Faerie Fire (Feral), Demoralizing Roar and Swipe (on 2+ attackers or 80 rage),
 *   Maul.
 *   Caster / moonkin damage: walk closer, Insect Swarm, Moonfire, Starfire (victim above 50%),
 *   Wrath.
 *
 * The Commander decides: Barkskin, Frenzied Regeneration (team rescue), Hibernate (crowd
 * control), Innervate (BoostBehavior), Nature's Swiftness (heal boosts), Hurricane (area
 * phase), Entangling Roots and running from melee (EscapeBehavior), dispels in combat.
 * Taken out 2026-11-14: Bash and Feral Charge on cooldown (InterruptBehavior assigns them,
 * ClassKit::Interrupts); a tank's Swipe and Demoralizing Roar (ThreatBehavior, ClassKit::
 * AreaThreat, as for the warrior tank); Faerie Fire only on a rogue victim (a PvP leftover).
 */

#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"
#include "Map.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Tanks.h"

namespace
{
    uint32 const ENRAGE_BELOW_RAGE = 200;       // 20 rage
    float const ENRAGE_MIN_HEALTH_PCT = 60.0f;
    float const SWIPE_RADIUS = 8.0f;            // as the warrior tank's Cleave check

    class DruidRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.druid;

            // Leave a combat form when the role changed to healer.
            if (bot.GetRole() == ROLE_HEALER && me->GetShapeshiftForm() != FORM_NONE &&
                me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            {
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                return;
            }

            SpellEntry const* pBuffSpell = nullptr;
            if (Player* pTarget = bot.SelectBuffTarget(s.pMarkoftheWild, s.pGiftoftheWild, pBuffSpell))
                if (Buff(pTarget, pBuffSpell))
                    return;

            if (s.pThorns)
                if (Unit* pTarget = SelectThornsTarget(s.pThorns))
                    if (Buff(pTarget, s.pThorns))
                        return;

            if (Cast(me, s.pNaturesGrasp))
                return;

            EndBuffing(s.pMarkoftheWild);

            if (me->GetShapeshiftForm() == FORM_NONE)
            {
                if (EnterCombatForm())
                    return;
                if (me->GetPowerPercent(POWER_MANA) > 80.0f && bot.FindAndHealInjuredAlly())
                    return;
            }
            else if (me->GetShapeshiftForm() == FORM_CAT)
            {
                DropUnwantedStealth(s.pProwl);
            }

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            ShapeshiftForm const form = me->GetShapeshiftForm();

            // A role changed in the fight (a stand-in tank, a spare healer: Commander
            // CrisisAssigner): out of a form that does not fit first. A healer used to stand
            // idle in it; a tank from cat form kept clawing. Bear Form follows from caster form.
            if (form != FORM_NONE && me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT) &&
               (bot.GetRole() == ROLE_HEALER ||
               (bot.m_role == ROLE_TANK && form != FORM_BEAR && form != FORM_DIREBEAR)))
            {
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                return;
            }

            if (form == FORM_NONE)
            {
                // A tank out of its form (rooted, a caster form spell of the Commander) goes back
                // to bear first: healing from caster form leaves the enemies loose on the party.
                if (bot.m_role == ROLE_TANK && EnterCombatForm())
                    return;
                // HoT or direct heal, chosen by ChooseHeal (HealSpells.h).
                if (bot.FindAndHealInjuredAlly(80.0f, 90.0f))
                    return;
                if (bot.GetRole() == ROLE_HEALER && bot.FindAndPreHealTarget())
                    return;
                if (EnterCombatForm())
                    return;
            }

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            // A healer deals damage by its tier this tick (healer damage tiers doc 2026-11-21,
            // section 6), from caster form, after the heal step above cast nothing: SPARE and FULL
            // Moonfire and Wrath; no free attack (no wand, no shapeshift, no melee).
            if (bot.GetRole() == ROLE_HEALER)
            {
                if (form == FORM_NONE && bot.HealerDamageNow() >= Commander::HEALER_DAMAGE_SPARE)
                    HealerCaster(pVictim);
                return;
            }

            switch (form)
            {
                case FORM_CAT:
                    Cat(pVictim);
                    break;
                case FORM_BEAR:
                case FORM_DIREBEAR:
                    Bear(pVictim);
                    break;
                case FORM_NONE:
                case FORM_MOONKIN:
                    Caster(pVictim);
                    break;
                default:
                    break;
            }
        }

    private:
        // Thorns only on the melee front (Tanks::IsMeleeFront: tanks, hunter pets, taunting
        // pets): on a damage dealer or a healer its damage is threat the tank must pull back.
        // Without a Commander (no party snapshot): nobody.
        Unit* SelectThornsTarget(SpellEntry const* pThorns) const
        {
            Player* me = m_bot.me;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            if (!commander)
                return nullptr;
            Commander::WorldModel const& world = commander->GetWorld();
            for (Commander::PartyMemberFacts const& m : world.party)
            {
                if (!m.alive || !Commander::Tanks::IsMeleeFront(world, m))
                    continue;
                Unit* pTarget = me->GetMap()->GetUnit(m.guid);
                if (!pTarget || !pTarget->IsInWorld() || !me->IsValidHelpfulTarget(pTarget) ||
                    !m_bot.IsValidBuffTarget(pTarget, pThorns) ||
                    m_bot.IsBuffPendingFromOther(pTarget, pThorns, nullptr) ||
                    !me->IsWithinDist(pTarget, 30.0f) || !me->IsWithinLOSInMap(pTarget))
                    continue;
                return pTarget;
            }
            return nullptr;
        }

        // Cat for melee damage, bear for a tank (or melee without cat), moonkin for ranged damage.
        bool EnterCombatForm()
        {
            TeamBotAI& bot = m_bot;
            auto const& s = bot.m_spells.druid;
            CombatBotRoles const role = bot.GetRole();

            if (role == ROLE_MELEE_DPS && Cast(bot.me, s.pCatForm))
                return true;
            if ((bot.m_role == ROLE_TANK || role == ROLE_MELEE_DPS) && Cast(bot.me, s.pBearForm))
                return true;
            if (role == ROLE_RANGE_DPS && Cast(bot.me, s.pMoonkinForm))
                return true;
            return false;
        }

        void MeleeChaseDistance()
        {
            if (m_bot.me->HasDistanceCasterMovement())
                m_bot.me->SetCasterChaseDistance(0.0f);
            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // tank / melee stance module chases)
        }

        void Cat(Unit* pVictim)
        {
            Player* me = m_bot.me;
            auto const& s = m_bot.m_spells.druid;
            MeleeChaseDistance();

            if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            {
                if (!Cast(pVictim, s.pPounce) && !Cast(pVictim, s.pRavage))
                    Cast(me, s.pTigersFury);
                return;
            }

            if (me->GetComboPoints() > 4 && (Cast(pVictim, s.pFerociousBite) || Cast(pVictim, s.pRip)))
                return;

            if (!me->CanReachWithMeleeAutoAttack(pVictim))
            {
                if (Cast(pVictim, s.pFaerieFireFeral))
                    return;
            }

            if (Cast(pVictim, s.pShred) || Cast(pVictim, s.pRake) || Cast(pVictim, s.pClaw))
                return;
        }

        void Bear(Unit* pVictim)
        {
            MeleeChaseDistance();
            if (m_bot.m_role == ROLE_TANK)
                BearTank(pVictim);
            else
                BearDamage(pVictim);
        }

        // Tank: Enrage when short of rage, Faerie Fire (Feral), Maul. Swipe, Demoralizing Roar and
        // Challenging Roar are the threat module's (ClassKit::AreaThreat), Growl the team rescue's,
        // Demoralizing Roar kept up for its damage reduction the mitigation module's
        // (ClassKit::AttackPowerDebuffs, MitigationBehavior).
        void BearTank(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.druid;
            uint32 const rage = me->GetPower(POWER_RAGE);

            // Enrage (1 min): 20 rage over 10 s, as the warrior tank's Bloodrage. Not when low:
            // it lowers armor while it runs.
            if (rage < ENRAGE_BELOW_RAGE && me->GetHealthPercent() > ENRAGE_MIN_HEALTH_PCT &&
                !me->GetAttackers().empty() && Cast(me, s.pEnrage))
                return;

            // No rage, 6 s cooldown: free threat and armor reduction.
            if (Cast(pVictim, s.pFaerieFireFeral))
                return;

            // Maul is the bear's main threat skill (not a rage dump like Heroic Strike): queued
            // whenever it can be paid. With 2+ enemies next to the victim the threat module may
            // want Swipe (global cooldown, the next swing would eat its rage first), so Maul only
            // with the rage of both.
            if (!s.pMaul)
                return;
            uint32 reserve = 0;
            if (s.pSwipe && me->GetEnemyCountInRadiusAround(pVictim, SWIPE_RADIUS) > 1)
                reserve = Spell::CalculatePowerCost(s.pSwipe, me);
            if (rage >= Spell::CalculatePowerCost(s.pMaul, me) + reserve)
                Cast(pVictim, s.pMaul);
        }

        // Melee damage without cat form: Swipe and Demoralizing Roar on 2+ attackers or 80 rage.
        void BearDamage(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            auto const& s = bot.m_spells.druid;

            if (Cast(pVictim, s.pFaerieFireFeral))
                return;

            if ((bot.me->GetPower(POWER_RAGE) > 800 || bot.GetAttackersInRangeCount(10.0f) > 1) &&
                (Cast(pVictim, s.pDemoralizingRoar) || Cast(pVictim, s.pSwipe)))
                return;

            Cast(pVictim, s.pMaul);
        }

        void Caster(Unit* pVictim)
        {
            Player* me = m_bot.me;
            auto const& s = m_bot.m_spells.druid;

            ChaseWhenOutOfRange(pVictim);

            if (Cast(pVictim, s.pInsectSwarm))
                return;

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return;

            if (Cast(pVictim, s.pMoonfire))
                return;
            if (pVictim->GetHealthPercent() > 50.0f && Cast(pVictim, s.pStarfire))
                return;
            Cast(pVictim, s.pWrath);
        }

        // A healer's damage (SPARE and FULL tiers): Moonfire, Wrath.
        void HealerCaster(Unit* pVictim)
        {
            auto const& s = m_bot.m_spells.druid;
            ChaseWhenOutOfRange(pVictim);
            if (Cast(pVictim, s.pMoonfire))
                return;
            Cast(pVictim, s.pWrath);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeDruid(TeamBotAI& bot)
{
    return std::make_unique<DruidRotation>(bot);
}
