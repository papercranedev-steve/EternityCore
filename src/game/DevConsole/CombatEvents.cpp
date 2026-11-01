#include "DevConsole/CombatEvents.h"
#include "Unit.h"
#include "Spell.h"
#include "Timer.h"

namespace DevConsole
{
    std::atomic<bool> CombatEvents::s_active{ false };
    std::mutex CombatEvents::s_mutex;
    std::unordered_set<uint64> CombatEvents::s_tracked;
    std::vector<CombatEvent> CombatEvents::s_buffer;
    uint32 CombatEvents::s_dropped = 0;

    // A drained buffer is expected every few hundred ms. This only protects memory if Lua stops draining.
    static size_t const MAX_BUFFERED_EVENTS = 200000;

    char const* CombatEventTypeName(CombatEventType type)
    {
        switch (type)
        {
            case CombatEventType::Damage:     return "dmg";
            case CombatEventType::Heal:       return "heal";
            case CombatEventType::CastStart:  return "cast";
            case CombatEventType::CastGo:     return "go";
            case CombatEventType::CastCancel: return "cancel";
            case CombatEventType::CastFail:   return "fail";
            case CombatEventType::AuraApply:  return "aura+";
            case CombatEventType::AuraRemove: return "aura-";
            case CombatEventType::Death:      return "death";
        }
        return "?";
    }

    char const* CastFailReasonName(int32 result)
    {
        switch (result)
        {
            case SPELL_FAILED_LINE_OF_SIGHT:    return "los";
            case SPELL_FAILED_OUT_OF_RANGE:     return "range";
            case SPELL_FAILED_TOO_CLOSE:        return "tooclose";
            case SPELL_FAILED_UNIT_NOT_INFRONT: return "facing";
            case SPELL_FAILED_MOVING:           return "moving";
            case SPELL_FAILED_NOT_READY:        return "notready";
            case SPELL_FAILED_NO_POWER:         return "nopower";
            case SPELL_FAILED_BAD_TARGETS:      return "badtarget";
            case SPELL_FAILED_TARGETS_DEAD:     return "targetdead";
            case SPELL_FAILED_SILENCED:         return "silenced";
            case SPELL_FAILED_STUNNED:          return "stunned";
            case SPELL_FAILED_INTERRUPTED_COMBAT: return "interruptedcombat";
        }
        return nullptr;
    }

    void CombatEvents::SetActive(bool active)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_active.store(active, std::memory_order_relaxed);
        if (!active)
        {
            s_tracked.clear();
            s_buffer.clear();
        }
    }

    void CombatEvents::SetTracked(std::vector<ObjectGuid> const& guids)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_tracked.clear();
        for (ObjectGuid const& guid : guids)
            s_tracked.insert(guid.GetRawValue());
    }

    std::vector<CombatEvent> CombatEvents::Drain()
    {
        std::vector<CombatEvent> out;
        std::lock_guard<std::mutex> lock(s_mutex);
        out.swap(s_buffer);
        return out;
    }

    uint32 CombatEvents::DroppedCount()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_dropped;
    }

    bool CombatEvents::IsTracked(ObjectGuid a, ObjectGuid b)
    {
        // caller holds s_mutex
        return (!a.IsEmpty() && s_tracked.count(a.GetRawValue())) || (!b.IsEmpty() && s_tracked.count(b.GetRawValue()));
    }

    void CombatEvents::Push(CombatEvent const& e)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_active.load(std::memory_order_relaxed) || !IsTracked(e.source, e.target))
            return;
        if (s_buffer.size() >= MAX_BUFFERED_EVENTS)
        {
            ++s_dropped;
            return;
        }
        s_buffer.push_back(e);
    }

    void CombatEvents::OnDamage(Unit const* attacker, Unit const* victim, uint32 damage, uint32 spellId, uint32 schoolMask, uint32 damageType)
    {
        CombatEvent e;
        e.timeMs = WorldTimer::getMSTime();
        e.type = CombatEventType::Damage;
        e.source = attacker ? attacker->GetObjectGuid() : ObjectGuid();
        e.target = victim ? victim->GetObjectGuid() : ObjectGuid();
        e.spellId = spellId;
        e.amount = int32(damage);
        e.extra = int32(damageType);
        e.school = schoolMask;
        Push(e);
    }

    void CombatEvents::OnHeal(WorldObject const* healer, Unit const* victim, uint32 amount, int32 gain, uint32 spellId, bool critical)
    {
        CombatEvent e;
        e.timeMs = WorldTimer::getMSTime();
        e.type = CombatEventType::Heal;
        e.source = healer ? healer->GetObjectGuid() : ObjectGuid();
        e.target = victim ? victim->GetObjectGuid() : ObjectGuid();
        e.spellId = spellId;
        e.amount = int32(amount);
        e.extra = gain;
        e.critical = critical;
        Push(e);
    }

    static void FillSpell(CombatEvent& e, Spell const* spell)
    {
        e.timeMs = WorldTimer::getMSTime();
        e.source = spell->GetCaster() ? spell->GetCaster()->GetObjectGuid() : ObjectGuid();
        e.target = spell->m_targets.getUnitTargetGuid();
        e.spellId = spell->m_spellInfo->Id;
        e.amount = spell->GetCastTime();
        e.school = spell->m_spellInfo->School;
    }

    void CombatEvents::OnCastStart(Spell const* spell)
    {
        CombatEvent e;
        FillSpell(e, spell);
        e.type = CombatEventType::CastStart;
        Push(e);
    }

    void CombatEvents::OnCastGo(Spell const* spell)
    {
        CombatEvent e;
        FillSpell(e, spell);
        e.type = CombatEventType::CastGo;
        Push(e);
    }

    void CombatEvents::OnCastCancel(Spell const* spell)
    {
        CombatEvent e;
        FillSpell(e, spell);
        e.type = CombatEventType::CastCancel;
        e.extra = int32(spell->GetCastedTime()); // remaining cast time in ms
        Push(e);
    }

    void CombatEvents::OnCastFail(Spell const* spell, uint32 result)
    {
        CombatEvent e;
        FillSpell(e, spell);
        e.type = CombatEventType::CastFail;
        e.extra = int32(result);
        Push(e);
    }

    void CombatEvents::OnAuraApply(Unit const* target, ObjectGuid caster, uint32 spellId, int32 durationMs, uint32 stacks)
    {
        CombatEvent e;
        e.timeMs = WorldTimer::getMSTime();
        e.type = CombatEventType::AuraApply;
        e.source = caster;
        e.target = target->GetObjectGuid();
        e.spellId = spellId;
        e.amount = durationMs;
        e.extra = int32(stacks);
        Push(e);
    }

    void CombatEvents::OnAuraRemove(Unit const* target, ObjectGuid caster, uint32 spellId, uint32 removeMode)
    {
        CombatEvent e;
        e.timeMs = WorldTimer::getMSTime();
        e.type = CombatEventType::AuraRemove;
        e.source = caster;
        e.target = target->GetObjectGuid();
        e.spellId = spellId;
        e.extra = int32(removeMode);
        Push(e);
    }

    void CombatEvents::OnDeath(Unit const* unit)
    {
        CombatEvent e;
        e.timeMs = WorldTimer::getMSTime();
        e.type = CombatEventType::Death;
        e.target = unit->GetObjectGuid();
        Push(e);
    }
}
