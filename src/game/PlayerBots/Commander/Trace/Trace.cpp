#include "Commander/Trace/Trace.h"
#include "Timer.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace Commander
{
namespace Trace
{
    namespace
    {
        struct Context
        {
            Sink* sink = nullptr;
            uint32 parent = 0;
            bool childAdded = false;            // something was recorded under `parent`
            std::string const* pin = nullptr;   // tag of the innermost pinning scope
        };
        thread_local Context t_context;

        std::string VFormat(char const* format, va_list args)
        {
            char buf[512];
            int const n = vsnprintf(buf, sizeof(buf), format, args);
            if (n < 0)
                return std::string();
            return std::string(buf, std::min<size_t>(size_t(n), sizeof(buf) - 1));
        }
    }

    void Sink::Arm(uint32 nowMs)
    {
        m_armed = true;
        m_armedAtMs = nowMs;
    }

    bool Sink::Armed(uint32 nowMs) const
    {
        return m_armed && WorldTimer::getMSTimeDiff(m_armedAtMs, nowMs) < ARM_MS;
    }

    std::vector<Record const*> Sink::Since(uint32 sinceId, bool& gap) const
    {
        std::vector<Record const*> out;
        gap = m_droppedMaxId > sinceId;
        for (Record const& r : m_ring)
            if (r.id > sinceId)
                out.push_back(&r);
        return out;
    }

    std::vector<Record> const* Sink::Pinned(std::string const& tag, uint32* version) const
    {
        auto const itr = m_pinned.find(tag);
        if (itr == m_pinned.end())
            return nullptr;
        if (version)
            *version = itr->second.version;
        return &itr->second.records;
    }

    void Sink::StartPin(std::string const& tag)
    {
        PinSet& set = m_pinned[tag];
        set.records.clear();
        set.version = ++m_pinVersion;
    }

    void Sink::Add(Record&& record, std::string const* pinTag)
    {
        if (pinTag)
        {
            PinSet& set = m_pinned[*pinTag];
            if (set.records.size() < PIN_MAX)
                set.records.push_back(record);
        }
        m_ring.push_back(std::move(record));
        uint32 const now = m_ring.back().timeMs;
        while (!m_ring.empty() &&
               (m_ring.size() > RING_MAX || WorldTimer::getMSTimeDiff(m_ring.front().timeMs, now) > RING_KEEP_MS))
        {
            m_droppedMaxId = std::max(m_droppedMaxId, m_ring.front().id);
            m_ring.pop_front();
        }
    }

    Use::Use(Sink* sink, uint32 nowMs)
        : m_prevSink(t_context.sink), m_prevParent(t_context.parent), m_prevPin(t_context.pin)
    {
        m_prevChildAdded = t_context.childAdded;
        m_nested = sink && sink == t_context.sink;
        if (m_nested)
            return;     // already recording into it (a pull planned by a clear in the refresh): keep the tree
        t_context.sink = sink && sink->Armed(nowMs) ? sink : nullptr;
        t_context.parent = 0;
        t_context.pin = nullptr;
        t_context.childAdded = false;
    }

    Use::~Use()
    {
        t_context.sink = m_prevSink;
        t_context.parent = m_prevParent;
        t_context.pin = m_prevPin;
        t_context.childAdded = m_prevChildAdded || (m_nested && t_context.childAdded);
    }

    bool On()
    {
        return t_context.sink != nullptr;
    }

    Scope::Scope(char const* kind, char const* pinTag)
    {
        m_sink = t_context.sink;
        if (!m_sink)
            return;
        m_record.id = m_sink->NextId();
        m_record.parent = t_context.parent;
        m_record.kind = kind;
        m_prevParent = t_context.parent;
        t_context.parent = m_record.id;
        m_prevChildAdded = t_context.childAdded;
        t_context.childAdded = false;
        m_prevPin = t_context.pin;
        if (pinTag)
        {
            m_pin = pinTag;
            m_sink->StartPin(m_pin);
            t_context.pin = &m_pin;
        }
    }

    Scope::~Scope()
    {
        if (!m_sink)
            return;
        t_context.parent = m_prevParent;
        // An empty scope (nothing below, no data: an assigner that calculated nothing) is left out.
        bool const keep = t_context.childAdded || !m_record.data.empty() || !m_pin.empty();
        t_context.childAdded = m_prevChildAdded || keep;
        if (keep)
        {
            m_record.timeMs = WorldTimer::getMSTime();
            // Pinned under its own tag too (the root of the set), and under an outer pin.
            std::string const* pin = m_pin.empty() ? m_prevPin : &m_pin;
            m_sink->Add(std::move(m_record), pin);
        }
        t_context.pin = m_prevPin;
    }

    void Scope::Label(char const* format, ...)
    {
        if (!m_sink)
            return;
        va_list args;
        va_start(args, format);
        m_record.label = VFormat(format, args);
        va_end(args);
    }

    void Emit(char const* kind, std::string label, std::string data, std::string input)
    {
        Sink* sink = t_context.sink;
        if (!sink)
            return;
        Record r;
        r.id = sink->NextId();
        r.parent = t_context.parent;
        r.timeMs = WorldTimer::getMSTime();
        r.kind = kind;
        r.label = std::move(label);
        r.data = std::move(data);
        r.input = std::move(input);
        sink->Add(std::move(r), t_context.pin);
        t_context.childAdded = true;
    }

    std::string Guid(ObjectGuid guid)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)guid.GetRawValue());
        return buf;
    }

    std::string Format(char const* format, ...)
    {
        va_list args;
        va_start(args, format);
        std::string s = VFormat(format, args);
        va_end(args);
        return s;
    }
}
}
