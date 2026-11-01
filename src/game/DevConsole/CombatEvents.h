/*
 * Combat event capture for the dev console recorder.
 *
 * Hooks in Unit/Spell/SpellCaster call the On* functions. They return immediately unless a
 * recording is active (one relaxed atomic load). While recording, events where one side is a
 * tracked unit are appended to a buffer; Lua drains it from the world thread (combatlog.drain()).
 *
 * Hooks run in map update threads, Track/Drain in the world thread, so the buffer and the tracked
 * set are protected by a mutex.
 */

#ifndef MANGOS_DEVCONSOLE_COMBATEVENTS_H
#define MANGOS_DEVCONSOLE_COMBATEVENTS_H

#include "Common.h"
#include "ObjectGuid.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

class WorldObject;
class Unit;
class Spell;

namespace DevConsole
{
    enum class CombatEventType : uint8
    {
        Damage,         // amount = damage dealt, extra = damage effect type (DamageEffectType)
        Heal,           // amount = heal, extra = effective heal (gain)
        CastStart,      // spell with a cast time started
        CastGo,         // spell went off
        CastCancel,     // cast interrupted / cancelled while preparing or channeling
        CastFail,       // cast refused by the spell checks (Spell::SendCastResult with an error):
                        // extra = SpellCastResult code, amount = cast time. Not for interrupts (see CastCancel).
        AuraApply,      // extra = stack amount / duration (ms) in amount
        AuraRemove,     // extra = remove mode
        Death,
    };

    char const* CombatEventTypeName(CombatEventType type);

    // Short name of a CastFail code for the common cases ("los", "range"...), nullptr for others.
    char const* CastFailReasonName(int32 result);

    struct CombatEvent
    {
        uint32 timeMs = 0;
        CombatEventType type = CombatEventType::Damage;
        ObjectGuid source;
        ObjectGuid target;
        uint32 spellId = 0;
        int32 amount = 0;
        int32 extra = 0;
        uint32 school = 0;
        bool critical = false;
    };

    class CombatEvents
    {
        public:
            static bool IsActive() { return s_active.load(std::memory_order_relaxed); }

            // world thread
            static void SetActive(bool active);
            static void SetTracked(std::vector<ObjectGuid> const& guids);
            static std::vector<CombatEvent> Drain();
            static uint32 DroppedCount();

            // hooks (any map thread)
            static void OnDamage(Unit const* attacker, Unit const* victim, uint32 damage, uint32 spellId, uint32 schoolMask, uint32 damageType);
            static void OnHeal(WorldObject const* healer, Unit const* victim, uint32 amount, int32 gain, uint32 spellId, bool critical);
            static void OnCastStart(Spell const* spell);
            static void OnCastGo(Spell const* spell);
            static void OnCastCancel(Spell const* spell);
            static void OnCastFail(Spell const* spell, uint32 result);
            static void OnAuraApply(Unit const* target, ObjectGuid caster, uint32 spellId, int32 durationMs, uint32 stacks);
            static void OnAuraRemove(Unit const* target, ObjectGuid caster, uint32 spellId, uint32 removeMode);
            static void OnDeath(Unit const* unit);

        private:
            static void Push(CombatEvent const& e);
            static bool IsTracked(ObjectGuid a, ObjectGuid b);

            static std::atomic<bool> s_active;
            static std::mutex s_mutex;
            static std::unordered_set<uint64> s_tracked;
            static std::vector<CombatEvent> s_buffer;
            static uint32 s_dropped;
    };
}

// Hook macro so the call sites in core files stay one line and cost one atomic load when idle.
#define DEVCONSOLE_COMBAT_EVENT(call) do { if (DevConsole::CombatEvents::IsActive()) DevConsole::CombatEvents::call; } while (0)

#endif
