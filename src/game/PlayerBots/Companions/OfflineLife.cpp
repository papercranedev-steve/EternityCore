#include "Companions/OfflineLife.h"
#include "Life/Errands/Trip/LessonPriority.h"
#include "Shared/Phrases.h"
#include "Shared/Gathering.h"
#include "Shared/Professions.h"
#include "Shared/SkillUpCost.h"
#include "Shared/Supplies.h"
#include "Shared/Vendor.h"
#include "Player.h"
#include "DBCStores.h"
#include <algorithm>
#include <ctime>

namespace Companions
{
    namespace OfflineLife
    {
        namespace
        {
            namespace P = BotShared::Professions;

            uint32 const CRAFT_PER_HOUR = 3;
            uint32 const CRAFT_PER_HOUR_CRAFTY = 4;
            uint32 const CRAFT_PER_DAY = 20;
            uint32 const CRAFT_PER_DAY_CRAFTY = 30;
            uint32 const GATHER_SECS = 8 * HOUR;
            uint32 const GATHER_SECS_GATHERER = 4 * HOUR;
            uint32 const GATHER_PER_DAY = 3;
            uint32 const GATHER_PER_DAY_GATHERER = 5;
            float const SPEND_SHARE = 0.5f;
            float const SPEND_SHARE_FRUGAL = 0.3f;
            float const SPEND_SHARE_SPENDTHRIFT = 0.8f;

            bool Has(std::vector<std::string> const& tags, char const* tag)
            {
                return std::find(tags.begin(), tags.end(), tag) != tags.end();
            }

            // The next rank of each primary profession, when it could learn it now (skill and level
            // reach the trainer's requirement; user 2026-11-21: no saving for a rank far away).
            uint32 NextRanksCost(Player const* bot)
            {
                uint32 cost = 0;
                for (uint32 skill : P::Primaries())
                {
                    if (!bot->HasSkill(uint16(skill)))
                        continue;
                    for (P::Rank const& rank : P::RanksOf(skill))
                        if (!bot->HasSpell(rank.spellId))
                        {
                            if (bot->GetSkillValuePure(uint16(skill)) >= rank.reqSkillValue && bot->GetLevel() >= rank.reqLevel)
                                cost += rank.cost;
                            break;
                        }
                }
                return cost;
            }


            struct Growing
            {
                uint32 skill;
                uint32 value;
                uint32 max;
                uint32 allowance;
                uint32 gained = 0;
                bool stoppedByMoney = false;
            };

            std::vector<Growing> Growable(Player const* bot, P::Kind kind, uint32 byTime, uint32 perDay,
                                             std::map<uint32, uint32> const& today)
            {
                std::vector<Growing> out;
                for (uint32 skill : P::Primaries())
                {
                    if (P::KindOf(skill) != kind || !bot->HasSkill(uint16(skill)))
                        continue;
                    // No Mining Pick, no Skinning Knife: no gathering (user 2026-11-21).
                    if (!BotShared::Gathering::HasTool(bot, skill))
                        continue;
                    auto done = today.find(skill);
                    uint32 const usedToday = done != today.end() ? done->second : 0;
                    uint32 const allowance = std::min(byTime, perDay > usedToday ? perDay - usedToday : 0);
                    out.push_back({ skill, bot->GetSkillValuePure(uint16(skill)), bot->GetSkillMaxPure(uint16(skill)), allowance });
                }
                return out;
            }

            std::string SkillName(uint32 skill, LocaleConstant locale)
            {
                SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
                if (!line)
                    return std::string();
                char const* name = line->name[locale] && *line->name[locale] ? line->name[locale] : line->name[0];
                return name ? name : "";
            }
        }

        Reserve ReserveOf(Player* bot, Life::Lessons::Context const& lessons)
        {
            Reserve r;
            r.supplies = BotShared::Vendor::ListPrice(bot, BotShared::Supplies::Missing(bot));
            r.repair = BotShared::Vendor::RepairCost(bot);
            r.lessons = Life::Lessons::CostUpTo(Life::Lessons::Available(bot, lessons), Life::Lessons::IMPORTANT);
            r.ranks = NextRanksCost(bot);
            return r;
        }

        Result Apply(Player* bot, Life::Lessons::Context const& lessons, std::vector<std::string> const& tags,
                     uint32 offlineSecs, float priceFactor, std::map<uint32, uint32>& today)
        {
            Result result;
            if (!bot || !offlineSecs)
                return result;

            // Crafting: bought point by point, the professions in turn.
            bool const crafty = Has(tags, "crafty");
            uint32 const perHour = crafty ? CRAFT_PER_HOUR_CRAFTY : CRAFT_PER_HOUR;
            uint32 const byTime = uint32(uint64(offlineSecs) * perHour / HOUR);
            std::vector<Growing> crafts = Growable(bot, P::Kind::Crafting, byTime,
                crafty ? CRAFT_PER_DAY_CRAFTY : CRAFT_PER_DAY, today);
            if (!crafts.empty())
            {
                float const share = Has(tags, "frugal") ? SPEND_SHARE_FRUGAL : Has(tags, "spendthrift") ? SPEND_SHARE_SPENDTHRIFT : SPEND_SHARE;
                uint32 const kept = ReserveOf(bot, lessons).Total();
                uint32 budget = bot->GetMoney() > kept ? uint32(float(bot->GetMoney() - kept) * share) : 0;
                for (bool changed = true; changed;)
                {
                    changed = false;
                    for (Growing& g : crafts)
                    {
                        if (g.stoppedByMoney || g.gained >= g.allowance || g.value >= g.max)
                            continue;
                        uint32 const cost = uint32(float(BotShared::SkillUpCost::OfPoint(g.skill, g.value)) * priceFactor + 0.5f);
                        if (cost > budget)
                        {
                            g.stoppedByMoney = true;
                            continue;
                        }
                        budget -= cost;
                        result.spent += cost;
                        ++g.value;
                        ++g.gained;
                        changed = true;
                    }
                }
                if (result.spent)
                    bot->ModifyMoney(-int32(result.spent));
            }

            // Gathering: free, slow.
            bool const gatherer = Has(tags, "gatherer");
            std::vector<Growing> gathers = Growable(bot, P::Kind::Gathering,
                offlineSecs / (gatherer ? GATHER_SECS_GATHERER : GATHER_SECS), gatherer ? GATHER_PER_DAY_GATHERER : GATHER_PER_DAY, today);
            for (Growing& g : gathers)
            {
                g.gained = std::min(g.allowance, g.max > g.value ? g.max - g.value : 0);
                g.value += g.gained;
            }

            for (std::vector<Growing> const* list : { &crafts, &gathers })
                for (Growing const& g : *list)
                {
                    if (g.stoppedByMoney && g.gained < g.allowance)
                        result.shortOfMoney = true;
                    if (!g.gained)
                        continue;
                    bot->SetSkill(uint16(g.skill), uint16(g.value), uint16(g.max));
                    today[g.skill] += g.gained;
                    result.gains.push_back({ g.skill, g.gained });
                }
            return result;
        }

        uint32 DayOf(time_t t)
        {
            std::tm const* local = std::localtime(&t);
            return local ? uint32(local->tm_year + 1900) * 1000 + uint32(local->tm_yday) : uint32(t / DAY);
        }

        std::string Line(Result const& result, std::vector<std::string> const& tags, LocaleConstant locale, BotShared::I18n::Lang lang)
        {
            BotShared::Phrases::Vars vars;
            if (result.gains.empty())
                return result.shortOfMoney ? BotShared::Phrases::Say(lang, "offline.broke", tags, vars) : std::string();

            std::string gains;
            std::string const comma = BotShared::Phrases::Word(lang, "place.comma");
            for (Gain const& g : result.gains)
            {
                if (!gains.empty())
                    gains += comma;
                gains += SkillName(g.skill, locale) + " +" + std::to_string(g.points);
            }
            vars["gains"] = gains;
            if (!result.spent)
                return BotShared::Phrases::Say(lang, "offline.skillup.free", tags, vars);
            vars["gold"] = BotShared::Phrases::Money(lang, result.spent);
            return BotShared::Phrases::Say(lang, "offline.skillup", tags, vars);
        }
    }
}
