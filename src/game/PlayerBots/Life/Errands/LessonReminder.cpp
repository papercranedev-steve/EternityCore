#include "Life/Errands/LessonReminder.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Capitals.h"
#include "Shared/NpcDirectory.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

namespace Life
{
namespace LessonReminder
{
    namespace
    {
        // A capital of its faction on this continent has its class trainer.
        bool TrainerOnContinent(Player const* me)
        {
            for (BotShared::Capitals::Capital const& capital : BotShared::Capitals::All())
            {
                if (capital.mapId != me->GetMapId() || capital.team != me->GetTeam())
                    continue;
                for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                         capital.mapId, capital.x, capital.y, capital.radius, UNIT_NPC_FLAG_TRAINER))
                {
                    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(n.entry);
                    if (info && info->trainer_type == TRAINER_TYPE_CLASS && info->trainer_class == me->GetClass())
                        return true;
                }
            }
            return false;
        }

        std::string NamesOf(std::vector<uint32> const& spells, LocaleConstant locale, BotShared::I18n::Lang lang)
        {
            std::string const comma = BotShared::Phrases::Word(lang, "place.comma");
            std::string names;
            for (uint32 spellId : spells)
                names += (names.empty() ? "" : comma) + Lessons::NameOf(spellId, locale, lang);
            return names;
        }
    }

    void Update(Player* me, LessonReminderState& state, Player* leader, Lessons::Context const& context,
                std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (now < state.nextCheckMs)
            return;
        state.nextCheckMs = now + CHECK_MS;
        if (!leader || leader->IsBot() || !leader->GetSession() || leader->IsInCombat() || !Errands::MayRun(me, leader))
            return;
        // (in a rest area the town trip asks, with the lessons listed)
        if (leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
            return;

        std::vector<uint32> pending;
        std::vector<uint32> fresh;
        for (Lessons::Listed const& lesson : Lessons::Available(me, context))
        {
            if (lesson.tier > Lessons::REMINDED)
                continue;
            pending.push_back(lesson.spellId);
            if (!state.told.count(lesson.spellId))
                fresh.push_back(lesson.spellId);
        }
        if (pending.empty() || !TrainerOnContinent(me))
            return;

        LocaleConstant const locale = leader->GetSession()->GetSessionDbcLocale();
        if (!fresh.empty())
        {
            BotShared::BotChat::SayToParty(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "lesson.remind", tags, { { "lessons", NamesOf(fresh, locale, BotShared::I18n::OfParty(me)) } }));
            state.told.insert(fresh.begin(), fresh.end());
            state.lastSaidMs = now;
        }
        else if (WorldTimer::getMSTimeDiff(state.lastSaidMs, now) > AGAIN_MS)
        {
            BotShared::BotChat::SayToParty(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "lesson.remind.again", tags, { { "lessons", NamesOf(pending, locale, BotShared::I18n::OfParty(me)) } }));
            state.lastSaidMs = now;
        }
    }
}
}
