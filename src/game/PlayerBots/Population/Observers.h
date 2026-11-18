/*
 * World population bots: observation points, the places where someone sees the world.
 *
 * The directors (city now, field later) fill the world around observers instead of around
 * online players, so a place can be filled before anyone is there:
 *  - an online real player is an observer at his position;
 *  - a character about to enter the world (account at the character screen, or a login
 *    request) is an "arriving" observer at his logout position, until he is in the world or
 *    PopBot.Prewarm.WaitSec passed.
 * World thread only (the hooks come from world-thread packet handlers).
 */

#ifndef MANGOS_POPULATION_OBSERVERS_H
#define MANGOS_POPULATION_OBSERVERS_H

#include "Common.h"
#include <vector>

namespace Population
{
    struct Observer
    {
        uint32 guid = 0;            // character
        uint32 accountId = 0;
        uint32 team = 0;
        uint32 mapId = 0;
        uint32 zone = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool arriving = false;      // not in the world yet: logout position
        uint32 expireMs = 0;        // arriving only
    };

    class Observers
    {
    public:
        void LoadConfig();

        // The account is at the character screen: expect its last played character.
        void ExpectAccount(uint32 accountId);
        // The client asked to enter the world with this character.
        void ExpectCharacter(uint32 accountId, uint32 guid);

        // Resolves pending hooks (characters table), drops arrived and expired arrivals,
        // re-reads online real players. True when a new arrival was added (fill right away).
        bool Update(uint32 nowMs);

        std::vector<Observer> const& All() const { return m_observers; }
        bool IsArrivingOwner(uint32 guid) const;

        // Could a player in the world see someone appear or vanish at this spot? Within the
        // map's visibility distance (plus a margin) and in line of sight; very close counts
        // even behind a wall. Arriving observers see nothing yet.
        bool CanSee(uint32 mapId, float x, float y, float z) const;

    private:
        struct Pending
        {
            uint32 accountId = 0;
            uint32 guid = 0;        // 0 = last played character of the account
        };

        bool Resolve(Pending const& pending, uint32 nowMs);

        bool m_enabled = true;
        uint32 m_waitMs = 90 * IN_MILLISECONDS;

        std::vector<Pending> m_pending;
        std::vector<Observer> m_arrivals;
        std::vector<Observer> m_observers;  // online players + arrivals
    };
}

#endif
