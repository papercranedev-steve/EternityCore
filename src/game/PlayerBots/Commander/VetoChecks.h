/*
 * PartyBot Commander: the checks official PartyBot code calls to honour vetoes.
 * One function per VetoType. Each call site is listed in
 * .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
 */

#ifndef MANGOS_COMMANDER_VETO_CHECKS_H
#define MANGOS_COMMANDER_VETO_CHECKS_H

class PartyBotAI;
class Unit;
class SpellEntry;
class SpellAuraHolder;

namespace Commander
{
namespace VetoChecks
{
    // A harmful aura on `target` that `dispelSpell` removes and that is not vetoed
    // (arbiter veto, urgent heal order, or hand-written dispel policy never / after combat).
    bool HasAllowedDebuff(PartyBotAI const* bot, Unit const* target, SpellEntry const* dispelSpell);

    // One aura: vetoed for this bot? Also every non-control aura while the bot holds an urgent
    // heal order (BotDirectives::HasUrgentHealOrder).
    bool IsDispelVetoed(PartyBotAI const* bot, Unit const* target, SpellAuraHolder const* holder);

    // The cast of `enemy` is not worth an interrupt (InterruptBehavior), or `enemy` is immune
    // to every interrupt / stun / silence the bot's class rotation would use on it.
    bool IsInterruptVetoed(PartyBotAI const* bot, Unit const* enemy);

    // Healing `target` now would pull aggro and is not needed yet (ThreatBehavior).
    bool IsHealVetoed(PartyBotAI const* bot, Unit const* target);

    // `spell` charges at `target` (SPELL_EFFECT_CHARGE: Charge, Intercept, Feral Charge) and a
    // module asked for no charging (PullBehavior: the tank brings the pack back).
    bool IsGapCloserVetoed(PartyBotAI const* bot, Unit const* target, SpellEntry const* spell);

    // `target` is a member of the bot's group mind controlled by a creature (Whitemane's
    // Dominate Mind). It turns hostile, so the original target selection would attack it and
    // the dispel selection would skip it as "not helpful" (combat scenario 7, problem 2).
    bool IsCharmedPartyMember(PartyBotAI const* bot, Unit const* target);
}
}

#endif
