#include "Companions/Affinity/AffinitySources.h"
#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/Gates.h"
#include "Companions/Affinity/Traits.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
#include <algorithm>
#include <string>

namespace Companions
{
    namespace AffinitySources
    {
        namespace
        {
            // Its party has a real player in it: the leader it follows.
            bool WithPlayer(Player* me, Player* leader)
            {
                return leader && !leader->IsBot() && me->GetGroup() && me->GetGroup() == leader->GetGroup();
            }

            void Gain(Player* me, Player* leader, uint32 points, char const* reason)
            {
                uint32 const now = sCompanionMgr.AddAffinity(me->GetGUIDLow(), points, reason);
                if (leader)
                    BotShared::BotChat::SystemTo(leader, "PBAFF;" + std::string(me->GetName()) + ";" + std::to_string(now) +
                        ";" + std::to_string(points) + ";" + reason);
            }

            // Friend traits (Traits) its affinity reached and it has not rolled yet. Also at the
            // first look (affinity it had before), then without a line. Every rolled one: PBGATE
            // "trait" (the addon reloads the companion list).
            void RollTraits(Player* me, Player* leader, State const& state, uint32 affinity, std::vector<std::string> const& tags)
            {
                uint32 const guid = me->GetGUIDLow();
                for (Traits::Tier tier : { Traits::Tier::Life, Traits::Tier::Combat })
                {
                    uint32 const at = Traits::TierAt(tier);
                    if (affinity < at)
                        continue;
                    TraitPair const have = sCompanionMgr.GetTraits(guid);
                    if ((tier == Traits::Tier::Life ? have.life : have.combat) != Traits::Trait::None)
                        continue;
                    Traits::Trait const trait = Traits::Roll(guid, tier, me->GetClass(), tags);
                    Traits::Info const* info = Traits::Of(trait);
                    if (!info)
                        continue;
                    sCompanionMgr.SetTrait(guid, tier, trait);
                    if (!leader)
                        continue;
                    bool const crossed = state.lastAffinity != UINT32_MAX && state.lastAffinity < at;
                    if (crossed)
                    {
                        Life::Phrases::Vars vars;
                        vars["leader"] = leader->GetName();
                        vars["trait"] = info->label;
                        std::string text = Life::Phrases::Say(std::string("affinity.trait.") + info->key, tags, vars);
                        if (text.empty())
                            text = Life::Phrases::Say(tier == Traits::Tier::Life ? "affinity.trait.life" : "affinity.trait.combat", tags, vars);
                        if (!text.empty())
                            BotShared::BotChat::SayToParty(me, text);
                    }
                    BotShared::BotChat::SystemTo(leader, "PBGATE;" + std::string(me->GetName()) + ";trait");
                }
            }
        }

        uint32 StartAt(uint32 level)
        {
            return std::min(level, START_CAP);
        }

        void Update(Player* me, Player* leader, State& state, uint32 paidCopper, std::vector<std::string> const& tags)
        {
            uint32 const level = me->GetLevel();
            uint32 const before = state.lastLevel ? state.lastLevel : level;
            state.lastLevel = level;
            uint32 const guid = me->GetGUIDLow();
            // (a trial partner has none yet: recruiting doc §2.2)
            if (!sCompanionMgr.IsBonded(guid))
                return;

            if (level > before && WithPlayer(me, leader))
                Gain(me, leader, (level - before) * PER_LEVEL, "levelup");

            if (paidCopper && sCompanionMgr.CountMoneyGift(guid, MAX_MONEY_GIFTS))
                Gain(me, leader, MONEY_GIFT, "gift");

            // What it allows now (any source: class quests, clears on the instance's thread ...).
            uint32 const affinity = sCompanionMgr.GetAffinity(guid);
            if (state.lastAffinity != UINT32_MAX && affinity > state.lastAffinity && leader)
            {
                Life::Phrases::Vars vars;
                vars["leader"] = leader->GetName();
                for (Gates::Info const* gate : Gates::Opened(state.lastAffinity, affinity, me->GetClass()))
                {
                    std::string const text = Life::Phrases::Say(std::string("affinity.unlock.") + gate->key, tags, vars);
                    if (!text.empty())
                        BotShared::BotChat::SayToParty(me, text);
                    BotShared::BotChat::SystemTo(leader, "PBGATE;" + std::string(me->GetName()) + ";" + gate->key);
                }
            }
            if (affinity != state.lastAffinity)
                RollTraits(me, leader, state, affinity, tags);
            state.lastAffinity = affinity;
        }

        ClearPoints PointsFor(BotShared::DungeonClears::Info const& segment)
        {
            if (segment.segment == BotShared::DungeonClears::Segment::Onyxia)
                return { 3, 2 };
            return segment.raid ? ClearPoints{ 5, 3 } : ClearPoints{ 2, 1 };
        }

        void OnClear(Map* map, BotShared::DungeonClears::Info const& segment)
        {
            if (!map)
                return;
            ClearPoints const points = PointsFor(segment);
            for (auto itr = map->GetPlayers().getFirst(); itr != nullptr; itr = itr->next())
            {
                Player* bot = itr->getSource();
                if (!bot || !bot->IsInWorld() || !sCompanionMgr.IsBonded(bot->GetGUIDLow()))
                    continue;
                // A real player of its party in the instance too (the one told about it).
                Player* player = nullptr;
                if (Group* group = bot->GetGroup())
                    for (GroupReference* ref = group->GetFirstMember(); ref && !player; ref = ref->next())
                        if (Player* member = ref->getSource())
                            if (!member->IsBot() && member->IsInWorld() && member->GetMap() == map)
                                player = member;
                if (!player)
                    continue;
                uint32 const times = sCompanionMgr.CountClear(bot->GetGUIDLow(), uint8(segment.segment));
                if (times)
                    Gain(bot, player, times == 1 ? points.first : points.later, times == 1 ? "firstclear" : "clear");
            }
        }
    }
}
