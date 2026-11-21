#include "Commander/Coordination/Situation/GatherPlanner.h"
#include "Shared/Phrases.h"
#include "Commander/Coordination/Situation/Summon.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/LeftBehind.h"
#include "Commander/Commands/BotSpeech.h"
#include "Commander/PartyCommander.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <set>

namespace Commander
{
namespace GatherPlanner
{
    namespace
    {
        float constexpr MOVED_YD = 2.0f;

        bool Free(SummonPlan const& s, Player* member)
        {
            return member && member->IsInWorld() && member->IsAlive() && !member->IsInCombat() &&
                   member->AI() && dynamic_cast<TeamBotAI*>(member->AI()) && !s.RitualOf(member->GetObjectGuid());
        }

        // Rituals whose warlock is gone or that nobody ended.
        void DropStale(SummonPlan& s, Map* map, uint32 nowMs)
        {
            s.rituals.erase(std::remove_if(s.rituals.begin(), s.rituals.end(), [&](SummonRitual const& r)
            {
                Player* w = map->GetPlayer(r.warlock);
                return !w || !w->IsAlive() || WorldTimer::getMSTimeDiff(r.sinceMs, nowMs) > RITUAL_GIVE_UP_MS;
            }), s.rituals.end());
        }
    }

    void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        Player* anchor = bot->GetPartyLeader();
        if (!anchor || !anchor->IsInWorld() || anchor->GetMap() != bot->me->GetMap())
            return;
        Map* map = anchor->GetMap();
        TeamPlan& plan = commander.GetPlanMutable();
        SummonPlan& s = plan.summon;
        DropStale(s, map, nowMs);

        // The team stands still (the leader settled, no fight).
        float const dx = anchor->GetPositionX() - s.settledX;
        float const dy = anchor->GetPositionY() - s.settledY;
        bool const still = anchor->IsAlive() && !anchor->IsMoving() && !anchor->IsInCombat() && !anchor->IsTaxiFlying() &&
                           !commander.GetWorld().HasPartyEnemy();
        if (!still || !s.settledSinceMs || s.settledMap != anchor->GetMapId() || dx * dx + dy * dy > MOVED_YD * MOVED_YD)
        {
            s.settledSinceMs = nowMs;
            s.settledMap = anchor->GetMapId();
            s.settledX = anchor->GetPositionX();
            s.settledY = anchor->GetPositionY();
            s.shortNoted = false;
            if (!s.gatherNow || !still)
                return;
        }
        if (map->IsDungeon() || map->IsBattleGround())
        {
            s.gatherNow = false;
            return;
        }
        bool const asked = s.gatherNow;
        if (!asked && WorldTimer::getMSTimeDiff(s.settledSinceMs, nowMs) < SETTLED_MS)
            return;
        s.gatherNow = false;

        std::vector<LeftBehindMember> left;
        for (LeftBehindMember const& st : LeftBehind::Of(anchor, Summon::FAR_YD))
            if (!s.RitualOf(st.member->GetObjectGuid()) && !Summon::RecentlySent(plan, st.member->GetObjectGuid(), nowMs))
                left.push_back(st);
        if (left.empty())
        {
            if (asked && s.rituals.empty())
                BotSpeech::Report(bot->me, anchor, BotShared::Phrases::Word(BotShared::I18n::OfParty(bot->me), "gather.all_here"));
            return;
        }

        // Who is with the team on this map: free warlocks with a shard to spare, and helpers.
        std::vector<Player*> warlocks, helpers;
        bool anyWarlock = false;
        for (Player* member : Team::Members(anchor))
        {
            if (!member || member == anchor || member->GetMap() != map || !Free(s, member) ||
                !member->IsWithinDist(anchor, WITH_TEAM_YD))
                continue;
            if (member->GetClass() == CLASS_WARLOCK && member->HasSpell(Summon::RITUAL_OF_SUMMONING))
            {
                anyWarlock = true;
                if (Summon::CanCast(member, Summon::SHARDS_KEPT_FOR_TEAMMATES))
                {
                    warlocks.push_back(member);
                    continue;
                }
            }
            helpers.push_back(member);
        }

        std::set<Player*> used;
        size_t next = 0;
        for (Player* warlock : warlocks)
        {
            if (next >= left.size())
                break;
            // Two helpers next to it; a warlock that cannot cast may help (already in `helpers`).
            std::vector<ObjectGuid> pair;
            for (Player* h : helpers)
                if (pair.size() < 2 && !used.count(h) && h->IsWithinDist(warlock, HELPER_YD))
                    pair.push_back(h->GetObjectGuid());
            if (pair.size() < 2)
                continue;
            Player* target = left[next].member;
            if (!Summon::Start(plan, warlock, target->GetObjectGuid(), pair))
                continue;
            ++next;
            for (Player* h : helpers)
                if (std::find(pair.begin(), pair.end(), h->GetObjectGuid()) != pair.end())
                    used.insert(h);
            BotSpeech::Report(warlock, anchor, BotShared::Phrases::Text(BotShared::I18n::OfParty(warlock), "gather.summoning", { { "name", target->GetName() } }));
        }

        // Left behind and nobody to summon them: say so once (no warlock at all: the old
        // catch up brings them, nothing to say).
        if (s.rituals.empty() && anyWarlock && warlocks.empty() && !s.shortNoted)
        {
            s.shortNoted = true;
            BotSpeech::Report(bot->me, anchor, BotShared::Phrases::Text(BotShared::I18n::OfParty(bot->me), "gather.no_shards", { { "count", std::to_string(left.size()) } }));
        }
    }

    bool SummonComing(Player* anchor, Player const* member)
    {
        if (!anchor || !anchor->IsInWorld() || !member)
            return false;
        Map const* map = anchor->GetMap();
        if (!map || map->IsDungeon() || map->IsBattleGround())
            return false;
        for (Player* m : Team::Members(anchor))
        {
            if (!m || m == member || m == anchor || !m->IsInWorld() || !m->IsAlive() || !m->IsInMap(anchor) ||
                m->GetClass() != CLASS_WARLOCK || !m->AI() || !dynamic_cast<TeamBotAI*>(m->AI()) ||
                !m->IsWithinDist(anchor, WITH_TEAM_YD))
                continue;
            if (Summon::CanCast(m, Summon::SHARDS_KEPT_FOR_TEAMMATES))
                return true;
        }
        return false;
    }
}
}
