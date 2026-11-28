/*
 * PartyBot Commander: boss modules, layered on top of the generic modules
 * (code-structure doc 3.4, architecture-layers doc).
 *
 * Unlike behaviors and assigners, encounter modules keep state (phase, when the adds came,
 * ...). So they are not shared: every PartyCommander creates its own instances from the
 * explicit list in EncounterRegistry.cpp (only the ones for its map), and only that
 * commander's map thread touches them.
 *
 * Call order on a perception refresh (PartyCommander::Update, map thread):
 *   WorldModel refresh
 *   -> pick the active module (Matches), Refresh (live objects allowed), ListEnemies, AddFacts
 *   -> assigners, situation stage (the doctrine: stamps)
 *   -> ShapeDoctrine
 *   -> assigners, tasks stage; duty modules offer their needs
 *   -> OfferDuties (adds needs, narrows the others'), one DutyAllocator run, duty modules apply
 *   -> AdjustPlan (what the stages above cannot say yet, e.g. plan.focus)
 *   -> notes and voice lines the module queued (Note, Say) go into the plan
 * A module speaks through the same things as the generic modules (facts, stamps, duty needs),
 * before the brain decides, rather than undoing its results afterwards (design doc
 * .kiro/docs/2026-11-27-encounter-layers.md).
 * Every combat tick of a bot: EncounterBehavior ("encounter") forwards Propose to the
 * active module of that bot's commander.
 * OnReset: when Matches turns false (fight over: no boss and no boss adds left), when the
 * "encounter" module is switched off, or when another module takes over.
 */

#ifndef MANGOS_COMMANDER_IENCOUNTER_H
#define MANGOS_COMMANDER_IENCOUNTER_H

#include "Common.h"
#include "Commander/CommanderTypes.h"
#include "Commander/Coordination/DutyAllocator.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <memory>
#include <string>
#include <vector>

class TeamBotAI;
class Creature;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class IEncounter
    {
    public:
        virtual ~IEncounter() = default;

        // Short id for logs and notes.
        virtual char const* GetId() const = 0;

        // Cheap: map id plus "is one of my entries in world.enemies". Called on every refresh
        // of a commander on this module's map.
        virtual bool Matches(WorldModel const& world, uint32 mapId) const = 0;

        // Map thread, once per refresh while active. The only place a module may read live
        // game objects (instance data, auras); `bot` is whichever party bot triggered the refresh.
        virtual void Refresh(TeamBotAI* /*bot*/, WorldModel const& /*world*/) {}

        // After Refresh, map thread (live objects allowed): creatures the party must kill though
        // none of them fights us (no party member on their threat lists, so the eyes do not see
        // them as enemies: Flamelash's Burning Spirits walking to him). The eyes fill their facts
        // into WorldModel::targeted (EnemyFacts::listed, WorldModelBuilder::AddListedEnemies),
        // so duty needs (OfferDuties) and targets can name them. Not a fight on their own
        // (WorldModel::HasPartyEnemy ignores targeted).
        virtual void ListEnemies(TeamBotAI* /*bot*/, WorldModel const& /*world*/, std::vector<Creature*>& /*out*/) const {}

        // Optional: add facts to the world model before the assigners run.
        virtual void AddFacts(WorldModel& /*world*/) {}

        // After the situation stage (the doctrine has stamped the enemies), before the tasks
        // and the duties: add stamps or change the doctrine's choices (TeamPlan::doctrine).
        // The duty modules read the result in the same refresh.
        virtual void ShapeDoctrine(WorldModel const& /*world*/, TeamPlan& /*plan*/) {}

        // After every duty module offered (`needs`), before the one allocation: add this
        // module's needs (DutyAllocator.h; a need of a kind is applied by that kind's module,
        // DutyKind::Intercept by FocusAssigner) and narrow the others' (take a candidate off a
        // need it must not answer, keep it in `rejected` with the reason for the log). Two
        // needs for one problem share a DutyKey in blockedBy / marks, so only one is answered.
        virtual void OfferDuties(WorldModel const& /*world*/, TeamPlan const& /*plan*/,
                                 std::vector<DutyNeed>& /*needs*/) const {}

        // After the assigners: change the plan they wrote, for what the hooks above cannot say
        // yet (plan.focus).
        virtual void AdjustPlan(WorldModel const& /*world*/, TeamPlan& /*plan*/) {}

        // Every combat tick of a bot of this party (through EncounterBehavior). Must not change
        // state: one call per bot, several bots per refresh.
        virtual void Propose(TeamBotAI const* /*bot*/, WorldModel const& /*world*/,
                             TeamPlan const& /*plan*/, std::vector<Proposal>& /*out*/) const {}

        // True while bots must not sit down to eat or drink (an event with waves: the next group
        // is already on its way). PartyCommander copies it into the plan every refresh
        // (TeamPlan::noRest -> ForbidsRest: RestBehavior, the arbiter); the Commander hook then
        // also runs out of combat (PartyCommander::WantsOutOfCombatControl). Return a value
        // computed in Refresh.
        virtual bool ForbidsRest() const { return false; }

        // An event with waves (boss doc 2.3): how they come, how many are left, when the next
        // one reaches the party (WaveEventFacts, the module's part). PartyCommander puts it in
        // WorldModel::waves after Refresh; the mana budget (WaveEvent::FutureMana) and the rest
        // rule (Coordination/Situation/WaveRest.h) read it. Leave `out` inactive when there is
        // none. Return values computed in Refresh.
        virtual void DescribeWaves(WaveEventFacts& /*out*/) const {}

        // Before a boss is pulled (boss doc G9): what must die first (BossPrep). Called for every
        // module of the map, active or not, on each refresh while the party does not fight;
        // facts only (world.packs: idle creatures within PACK_SCAN_RADIUS of the leader).
        // PartyCommander puts the result in TeamPlan::bossPreps: the clear passes over the boss
        // while it is not done (ClearPlanner), the party tells the player when he comes near it.
        virtual void BeforePull(WorldModel const& /*world*/, std::vector<BossPrep>& /*out*/) const {}

        // Fight over or module switched off: forget everything about this attempt.
        virtual void OnReset() = 0;

        // PartyCommander, after AdjustPlan: the queued notes into plan.notes, the voice lines
        // into plan.voice (the party voice says them on the same refresh).
        void FlushMessages(TeamPlan& plan)
        {
            for (std::string& note : m_notes)
                plan.notes.push_back(std::move(note));
            m_notes.clear();
            for (VoiceEvent& line : m_voice)
                plan.voice.push_back(std::move(line));
            m_voice.clear();
        }

    protected:
        // From Refresh or AdjustPlan: a debug note for the note log, a line for the players
        // (EncounterHelpers::Line). Put into the plan on the same refresh.
        void Note(std::string text) { m_notes.push_back(std::move(text)); }
        void Say(VoiceEvent line) { m_voice.push_back(std::move(line)); }

    private:
        std::vector<std::string> m_notes;
        std::vector<VoiceEvent> m_voice;
    };

    using EncounterList = std::vector<std::unique_ptr<IEncounter>>;
}

#endif
