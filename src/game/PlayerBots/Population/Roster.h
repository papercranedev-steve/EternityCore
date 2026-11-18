/*
 * World population bots: the fixed roster. Every real character ("owner") gets a cohort of
 * people who started playing with it; they grow with the owner's progress and keep their
 * names and looks. A cohort whose owner was deleted turns free: its anchor is frozen and its
 * members live on as elders.
 * Design: .kiro/docs/2026-11-17-fixed-roster.md (workspace root).
 *
 * Members are identities only (characters DB tables custom_popbot_*, created on first load);
 * the character itself is a temporary bot, until a real player recruits it: then it is a saved
 * character (char_guid, Companions/CompanionMgr) and the roster no longer spawns it. World thread only.
 */

#ifndef MANGOS_POPULATION_ROSTER_H
#define MANGOS_POPULATION_ROSTER_H

#include "Common.h"
#include "Population/PopulationTypes.h"
#include <random>
#include <set>
#include <string>
#include <vector>

class Player;

namespace Population
{
    class Identity;
    class Observers;

    struct Cohort
    {
        uint32 id = 0;
        uint32 ownerGuid = 0;
        std::string ownerName;
        uint32 team = 0;            // owner's team
        uint8 maxLevel = 1;         // anchor: highest level the owner reached
        uint32 gearCap = 0;         // anchor: best average item level worn in the owner's group
        bool free = false;          // owner deleted: anchor frozen, members are elders for everyone

        bool ownerOnline = false;   // runtime
    };

    struct RosterMember
    {
        uint32 id = 0;
        uint32 cohortId = 0;
        std::string name;
        uint8 race = 0, classId = 0;
        PlayerBotAppearance look;
        uint32 homeZone = 0;        // city zone id
        float pace = 1.0f;          // level = anchor * pace (not used when stallLevel is set)
        uint8 stallLevel = 0;       // != 0: stays at this level for good
        uint8 quitLevel = 0;        // != 0: stops logging in once the anchor passed this level
        int8 jitter = 0;            // -1 / 0 / +1 level
        uint8 wealth = 0;           // 0-100, gear and money (phase R2)
        uint8 temperament = 0;      // unused, personality is `tags`
        std::vector<std::string> tags;  // personality tag keys (Companions/Personality.h)
        uint32 charGuid = 0;        // != 0: recruited, a real character now (Companions/); never spawned from the roster

        // Runtime
        bool inUse = false;         // online or queued
        uint32 availableAtMs = 0;   // PopulationMgr clock, cooldown after logging out
        uint32 sessionGuid = 0;     // character guid used this server run (name cache stays bound to it)
    };

    struct RosterQuery
    {
        uint32 team = 0;
        uint32 zone = 0;            // members from this city are preferred
        uint8 minLevel = 1, maxLevel = 60;
        bool preferHigh = false;    // one of the highest levels (city veterans)
    };

    struct RosterPick
    {
        uint32 memberId = 0;
        std::string name;
        uint8 race = 0, classId = 0, level = 0;
        PlayerBotAppearance look;
        uint32 sessionGuid = 0;     // 0 = first time online this run
        GearPlan gear;
        uint32 money = 0;           // copper
    };

    struct RosterCohortStats
    {
        Cohort cohort;
        uint32 members = 0, retired = 0, enemy = 0, inUse = 0;
        uint32 byTen[7] = {};       // levels 1-9, 10-19, ... 60
    };

    class Roster
    {
    public:
        void LoadConfig();
        // Creates the tables when missing, reads the seed, cohorts and members.
        // Cohorts of deleted (or renamed-over) characters are turned free.
        void Load();
        // Every 5 s: real players online get a cohort (generated once), owner levels are stored.
        // Characters about to log in (Observers) count as online owners.
        void Update(uint32 nowMs, Identity const& identity, Observers const& observers);
        // Run the next update right away (a character is about to log in).
        void ForceUpdate() { m_nextUpdateMs = 0; }

        bool IsActive() const { return m_enabled && m_available; }
        bool IsAvailable() const { return m_available; }
        uint64 GetSeed() const { return m_seed; }

        // Picks a member that is not online, not cooling down and not retired; marks it in use.
        bool Pick(RosterQuery const& query, uint32 nowMs, RosterPick& out);
        void SetSessionGuid(uint32 memberId, uint32 guid);
        // Recruited as a companion: the member is a real character from now on.
        void SetCharGuid(uint32 memberId, uint32 charGuid);
        void MarkInUse(uint32 memberId);
        // Logged out (or failed to spawn): free again after the cooldown.
        void Release(uint32 memberId, uint32 nowMs);
        void AddNamesTo(std::set<std::string>& names) const;

        uint8 LevelOf(RosterMember const& member) const;
        std::vector<RosterCohortStats> GetStats() const;

    private:
        bool EnsureTables();
        void LoadSeed();
        void LoadCohorts();
        void LoadMembers();
        void LoadTags();
        // Members without personality tags (new, or from before tags existed) get theirs, seeded by id.
        void AssignMissingTags();
        void FreeCohort(Cohort& cohort);
        Cohort* FindCohort(uint32 id);
        Cohort const* FindCohort(uint32 id) const;
        Cohort* FindOwnedCohort(uint32 ownerGuid);
        RosterMember* FindMember(uint32 id);
        void CreateCohort(Player* owner, Identity const& identity);
        // Generates members until the cohort has PopBot.Roster.CohortSize (also after raising it).
        void FillCohort(Cohort const& cohort, Identity const& identity);
        void UpdateAnchor(Cohort& cohort, Player* owner);
        bool GenerateMember(std::mt19937& rng, uint32 cohortTeam, Identity const& identity,
            std::set<std::string>& reserved, RosterMember& out) const;

        bool m_enabled = true;
        bool m_available = false;
        uint32 m_cohortSize = 300;
        uint8 m_leadCap = 3;
        uint32 m_stallPct = 15;
        uint32 m_leaderPct = 10;
        uint32 m_quitPct = 30;
        uint32 m_enemyPct = 10;
        uint32 m_elderSharePct = 30;
        uint32 m_cooldownMs = 30 * MINUTE * IN_MILLISECONDS;

        uint64 m_seed = 0;
        uint32 m_nextUpdateMs = 0;
        std::vector<Cohort> m_cohorts;
        std::vector<RosterMember> m_members;
    };
}

#endif
