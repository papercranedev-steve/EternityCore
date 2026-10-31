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
    // (arbiter veto, or hand-written dispel policy never / after combat).
    bool HasAllowedDebuff(PartyBotAI const* bot, Unit const* target, SpellEntry const* dispelSpell);

    // One aura: vetoed for this bot?
    bool IsDispelVetoed(PartyBotAI const* bot, Unit const* target, SpellAuraHolder const* holder);
}
}

#endif
