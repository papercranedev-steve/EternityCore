/*
 * PartyBot Commander: calculation trace for the combat recorder (user, 2026-11-08: "if the
 * score of every step is unknown the log is not good enough"; doc
 * .kiro/docs/2026-11-08-calc-trace.md).
 *
 * The valuations are pure functions (FightLoad::Simulate, ControlValue::Of, ...). Instead of
 * logging at every call site, the functions record themselves: their input and result as JSON,
 * under the scope that called them. A scope is a node of a call tree:
 *
 *   brain refresh                      PartyCommander::Update   (Use + Scope)
 *     assigner cc                      PartyCommander::RunAssigners
 *       control Overi Shackle Undead -> Patchwork Horror   ControlValue::Of (worth terms)
 *         fight "as is"                FightLoad::Simulate (input + result)
 *         fight "controlled"
 *         fight "freed"
 *
 * Off unless armed: the recorder polls the leader's commander (u:trace) and every poll arms it
 * for ARM_MS. Without a sink installed on this thread (Use) nothing is built, so the cost when
 * nobody records is one thread-local read per call.
 *
 * Records live in the PartyCommander's Sink: a ring of the last RING_KEEP_MS (the recorder keeps
 * its own short buffer and writes the windows around anomalies), plus pinned sets: a scope
 * opened with a pin tag (the pull planning) copies everything under it into Pinned(tag),
 * replacing the previous set with that tag, so the recorder can write it when the fight starts.
 *
 * Threads: a Sink belongs to one PartyCommander (map thread of its party); the dev console
 * reads it in the world thread after the maps updated. The current sink is thread-local.
 */

#ifndef MANGOS_COMMANDER_TRACE_H
#define MANGOS_COMMANDER_TRACE_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Commander
{
namespace Trace
{
    uint32 const ARM_MS = 3000;
    uint32 const RING_KEEP_MS = 8000;
    size_t const RING_MAX = 40000;
    size_t const PIN_MAX = 8000;

    struct Record
    {
        uint32 id = 0;
        uint32 parent = 0;      // 0: a root
        uint32 timeMs = 0;
        std::string kind;       // "brain", "assigner", "fight", "control", "patrol", ...
        std::string label;      // what it is about, for people ("as is", "Overi Shackle Undead -> ...")
        std::string data;       // JSON object: result / values (may be empty)
        std::string input;      // JSON object: full input of a pure calculation that can be re-run
                                // offline (FightLoad::Input), else empty
    };

    class Sink
    {
    public:
        void Arm(uint32 nowMs);
        bool Armed(uint32 nowMs) const;

        // Records with id > sinceId, in the order they were added (a scope after its children).
        // `gap`: some of them were already dropped.
        std::vector<Record const*> Since(uint32 sinceId, bool& gap) const;
        // The set last pinned with `tag` (nullptr: none yet) and a counter that changes with it.
        std::vector<Record> const* Pinned(std::string const& tag, uint32* version = nullptr) const;

        // (Scope / Emit)
        uint32 NextId() { return m_nextId++; }
        void Add(Record&& record, std::string const* pinTag);
        void StartPin(std::string const& tag);

    private:
        struct PinSet
        {
            std::vector<Record> records;
            uint32 version = 0;
        };
        uint32 m_nextId = 1;
        uint32 m_armedAtMs = 0;
        bool m_armed = false;
        uint32 m_pinVersion = 0;
        uint32 m_droppedMaxId = 0;
        // Insertion order: a scope is added when it ends, after its children (ids are given
        // when they start, so a parent's id is lower than its children's).
        std::deque<Record> m_ring;
        std::map<std::string, PinSet> m_pinned;
    };

    // Installs `sink` as this thread's current sink while it lives, if the sink is armed
    // (else none: nothing is recorded below). Restores the previous one.
    class Use
    {
    public:
        Use(Sink* sink, uint32 nowMs);
        ~Use();
        Use(Use const&) = delete;
        Use& operator=(Use const&) = delete;
    private:
        Sink* m_prevSink;
        uint32 m_prevParent;
        std::string const* m_prevPin;
        bool m_prevChildAdded;
        bool m_nested = false;
    };

    // Something is being recorded on this thread.
    bool On();

    // A node of the call tree: records below it get it as parent. Written when it ends (with the
    // label and data set meanwhile). With `pinTag`: everything under it is also kept in
    // Sink::Pinned(pinTag).
    class Scope
    {
    public:
        explicit Scope(char const* kind, char const* pinTag = nullptr);
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

        bool On() const { return m_sink != nullptr; }
        void Label(char const* format, ...) ATTR_PRINTF(2, 3);
        void SetLabel(std::string label) { if (m_sink) m_record.label = std::move(label); }
        void Data(std::string json) { if (m_sink) m_record.data = std::move(json); }

    private:
        Sink* m_sink = nullptr;
        Record m_record;
        uint32 m_prevParent = 0;
        bool m_prevChildAdded = false;
        std::string m_pin;
        std::string const* m_prevPin = nullptr;
    };

    // A leaf record under the current scope.
    void Emit(char const* kind, std::string label, std::string data, std::string input = std::string());

    // "0xF13000288D00CBD5" (the recorder's guid format).
    std::string Guid(ObjectGuid guid);
    // printf into a std::string.
    std::string Format(char const* format, ...) ATTR_PRINTF(1, 2);
}
}

#endif
