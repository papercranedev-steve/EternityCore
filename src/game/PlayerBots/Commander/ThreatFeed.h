/*
 * PartyBot Commander: threat snapshots for the PartyBot addon's threat meter.
 *
 * The 1.12 protocol has no threat packets (SMSG_THREAT_UPDATE is 2.x) and HermesProxy does not
 * make them up, so the 1.14 client's own threat display stays empty. Instead, players who
 * subscribed (`.partybot ai threat watch target|all`) get addon messages with the prefix
 * PBTHR (PlayerChat::SendAddonMessage).
 *
 * Data comes from the party's WorldModel after each refresh (map thread), so only enemies
 * fighting the party are known, and only while a party bot is on the player's map.
 *
 * One snapshot = several messages (each <= 255 bytes, fields separated by ';'):
 *   B                                                  begin
 *   T;<name>;<hp%>;<victim name>                       the player's selection
 *   R;<name>;<threat>;<per sec>;<headroom>;<flags>      one row of its threat list, highest first
 *        headroom: threat left before taking aggro, "-" for the victim
 *        flags: v = current victim, m = in melee reach (110%, else 130%), n = not on the list
 *               yet, o = not of the party (ally NPC)
 *   A;<name>;<hp%>;<victim>;<victim threat>;<lead>;<closest>;<second>;<flags>;<second name>
 *        mode "all" only: every enemy
 *        lead: threat the closest other member is still away from taking it ("-" = nobody)
 *        second: highest threat of anyone on its list but the victim (0 = nobody)
 *        flags: t = the victim is a tank (Perception/Tanks.h)
 *        second name: who has <second> ("-" = nobody)
 *   E                                                  end
 * No selection or the selection is not fighting the party: B, (A lines,) E without T / R.
 * Nothing at all is sent while there is nothing to show, except one empty B/E to clear.
 */

#ifndef MANGOS_COMMANDER_THREAT_FEED_H
#define MANGOS_COMMANDER_THREAT_FEED_H

#include "Common.h"
#include <map>

class PartyBotAI;

namespace Commander
{
    struct WorldModel;

    enum class ThreatWatch : uint8
    {
        Off = 0,
        Target = 1,     // the selected enemy's threat list
        All = 2,        // plus one line per enemy fighting the party (debugging)
    };

    char const* ThreatWatchName(ThreatWatch mode);

    // Per PartyCommander; map thread only.
    class ThreatFeed
    {
    public:
        static uint32 const INTERVAL_MS = 500;

        // After a world model refresh. Sends to every subscribed real player of the bot's
        // group on the bot's map, at most once per INTERVAL_MS.
        void Push(PartyBotAI* bot, WorldModel const& world, uint32 nowMs);

    private:
        uint32 m_lastPushMs = 0;
        std::map<uint64, bool> m_sentEmpty;     // player guid -> last snapshot had nothing in it
    };
}

#endif
