#include "Commander/ThreatFeed.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ThreatView.h"
#include "PartyBotAI.h"
#include "Chat.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"
#include "Timer.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Commander
{
    namespace
    {
        char const* const PREFIX = "PBTHR";
        size_t const MAX_ROWS = 10;
        size_t const MAX_ENEMY_LINES = 12;

        std::string NameOf(Map* map, ObjectGuid guid)
        {
            if (guid.IsEmpty())
                return "-";
            if (Unit* unit = map->GetUnit(guid))
                return unit->GetName();
            return "?";
        }

        std::string Num(float value)
        {
            if (std::isinf(value) || std::isnan(value))
                return "-";
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f", value);
            return buf;
        }

        // Addon message to the player himself: whisper type so the 1.12 packet carries a sender,
        // LANG_ADDON with "prefix\tbody" as HermesProxy expects (ChatPkt::CheckAddonPrefix).
        void Send(Player* player, std::string const& body)
        {
            std::string const text = std::string(PREFIX) + "\t" + body;
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_ADDON, CHAT_TAG_NONE,
                player->GetObjectGuid(), player->GetName());
            player->GetSession()->SendPacket(&data);
        }

        void AddTarget(Map* map, EnemyFacts const& e, std::vector<std::string>& out)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "T;%s;%.0f;%s", NameOf(map, e.guid).c_str(), e.healthPct,
                NameOf(map, e.victim).c_str());
            out.push_back(buf);

            size_t rows = 0;
            for (ThreatEntry const& t : e.threat)
            {
                if (++rows > MAX_ROWS)
                    break;
                std::string flags;
                if (t.guid == e.victim)
                    flags += 'v';
                if (t.inMeleeReach)
                    flags += 'm';
                if (!t.onList)
                    flags += 'n';
                if (!t.partySide)
                    flags += 'o';
                float const headroom = t.partySide ? ThreatView::Headroom(e, t.guid) : ThreatView::NO_LIMIT;
                snprintf(buf, sizeof(buf), "R;%s;%s;%s;%s;%s", NameOf(map, t.guid).c_str(), Num(t.threat).c_str(),
                    Num(t.threatPerSec).c_str(), t.guid == e.victim ? "-" : Num(headroom).c_str(), flags.c_str());
                out.push_back(buf);
            }
        }

        void AddEnemyLines(Map* map, WorldModel const& world, std::vector<std::string>& out)
        {
            size_t lines = 0;
            for (EnemyFacts const& e : world.enemies)
            {
                if (++lines > MAX_ENEMY_LINES)
                    break;
                ObjectGuid closest;
                float const lead = ThreatView::VictimLead(e, &closest);
                char buf[256];
                snprintf(buf, sizeof(buf), "A;%s;%.0f;%s;%s;%s;%s", NameOf(map, e.guid).c_str(), e.healthPct,
                    NameOf(map, e.victim).c_str(), Num(e.victimThreat).c_str(), Num(lead).c_str(),
                    NameOf(map, closest).c_str());
                out.push_back(buf);
            }
        }
    }

    char const* ThreatWatchName(ThreatWatch mode)
    {
        switch (mode)
        {
            case ThreatWatch::Target: return "target";
            case ThreatWatch::All:    return "all";
            default:                  return "off";
        }
    }

    void ThreatFeed::Push(PartyBotAI* bot, WorldModel const& world, uint32 nowMs)
    {
        if (m_lastPushMs && WorldTimer::getMSTimeDiff(m_lastPushMs, nowMs) < INTERVAL_MS)
            return;
        m_lastPushMs = nowMs;

        Group* group = bot->me->GetGroup();
        if (!group)
            return;
        Map* map = bot->me->GetMap();
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != map || !member->GetSession())
                continue;
            ThreatWatch const mode = sCommanderMgr.GetThreatWatch(member->GetObjectGuid());
            if (mode == ThreatWatch::Off)
                continue;

            std::vector<std::string> lines;
            if (EnemyFacts const* target = world.FindEnemy(member->GetSelectionGuid()))
                AddTarget(map, *target, lines);
            if (mode == ThreatWatch::All)
                AddEnemyLines(map, world, lines);

            bool& sentEmpty = m_sentEmpty[member->GetObjectGuid().GetRawValue()];
            if (lines.empty() && sentEmpty)
                continue;
            sentEmpty = lines.empty();

            Send(member, "B");
            for (std::string const& line : lines)
                Send(member, line);
            Send(member, "E");
        }
    }
}
