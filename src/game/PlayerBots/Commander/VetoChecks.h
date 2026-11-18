/*
 * PartyBot Commander: the checks official PartyBot code calls to honour vetoes.
 * One function per VetoType. Each call site is listed in
 * .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
 */

#ifndef MANGOS_COMMANDER_VETO_CHECKS_H
#define MANGOS_COMMANDER_VETO_CHECKS_H

class TeamBotAI;
class Unit;
class SpellEntry;
class SpellAuraHolder;

namespace Commander
{
namespace VetoChecks
{
    // A harmful aura on `target` that `dispelSpell` removes and that is not vetoed
    // (urgent heal order, or hand-written dispel policy never / after combat).
    bool HasAllowedDebuff(TeamBotAI const* bot, Unit const* target, SpellEntry const* dispelSpell);

    // One aura: may the rotation not dispel it (out of combat; in combat the dispel module
    // decides)? Hand-written policy never / after combat, and every non-control aura while the
    // bot holds an urgent heal order.
    bool IsDispelVetoed(TeamBotAI const* bot, Unit const* target, SpellAuraHolder const* holder);

    // Healing `target` now would pull aggro and is not needed yet (ThreatBehavior).
    bool IsHealVetoed(TeamBotAI const* bot, Unit const* target);

    // `target` is a member of the bot's group mind controlled by a creature (Whitemane's
    // Dominate Mind). It turns hostile, so the original target selection would attack it and
    // the dispel selection would skip it as "not helpful" (combat scenario 7, problem 2).
    bool IsCharmedPartyMember(TeamBotAI const* bot, Unit const* target);
}
}

#endif
