/*
 * PartyBot Commander layer 1 (perception): creatures the team's quests still need
 * (PackCreature::questNeeded; the active pull stance pulls them, Situation/Pull/AutoClear.h).
 *
 * Needed by a member (the player or a bot of the team, each its own quest log) when one of his
 * current quests still counts kills of it (ReqCreatureOrGOId, not yet ReqCreatureOrGOCount;
 * objectives done by a spell on it are not kills), or it can drop a quest item he still needs
 * (the server's own loot rule, LootStore::HaveQuestLootForPlayer).
 *
 * One cache per pack refresh: kill objectives are read once per member, drops once per entry.
 */

#ifndef MANGOS_COMMANDER_QUEST_NEEDS_H
#define MANGOS_COMMANDER_QUEST_NEEDS_H

#include "Common.h"
#include <map>
#include <set>
#include <vector>

class Player;

namespace Commander
{
namespace QuestNeeds
{
    class Cache
    {
    public:
        explicit Cache(std::vector<Player*> members);
        bool Needs(uint32 creatureEntry);

    private:
        std::vector<Player*> m_members;
        std::set<uint32> m_kills;               // entries some member still counts kills of
        std::map<uint32, bool> m_byEntry;       // answers so far
    };
}
}

#endif
