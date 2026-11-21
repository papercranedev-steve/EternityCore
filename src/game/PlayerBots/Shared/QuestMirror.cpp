#include "Shared/QuestMirror.h"
#include "Shared/Phrases.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "GossipDef.h"
#include "WorldSession.h"
#include "Log.h"
#include <algorithm>
#include <unordered_map>

namespace BotShared
{
    namespace QuestMirror
    {
        namespace
        {
            // The client shows a counter's own text when it has one; the entry only has to exist.
            // Item and Done counters count these creatures (GM-only waypoints and test triggers):
            // nobody fights them, and a mirror never counts kills anyway.
            int32 const STAND_IN_CREATURES[QUEST_OBJECTIVES_COUNT] = { 1, 2, 13056, 11179 };
            uint32 const MAX_COUNT = 63;

            // Texts of a mirror in the language of a database locale (i18n "mirror.*": Chinese for
            // zhCN / zhTW, else English). Quest texts go to the client by its locale, not by the
            // account's language.
            struct Texts
            {
                std::string prefix;
                std::string mirrorIntro;
                std::string placeholderIntro;   // {giver}
                std::string placeholderGoal;    // {giver}
                std::string done;
                std::string someGiver;
            };

            // -1: the base (enUS) strings.
            Texts TextsFor(int localeIdx)
            {
                I18n::Lang const lang = localeIdx < 0 ? I18n::Lang::En : I18n::OfLocale(sObjectMgr.GetLocaleForIndex(localeIdx));
                Texts t;
                t.prefix = Phrases::Word(lang, "mirror.prefix");
                t.mirrorIntro = Phrases::Word(lang, "mirror.intro");
                t.placeholderIntro = Phrases::Word(lang, "mirror.placeholder.intro");
                t.placeholderGoal = Phrases::Word(lang, "mirror.placeholder.goal");
                t.done = Phrases::Word(lang, "mirror.done");
                t.someGiver = Phrases::Word(lang, "mirror.somegiver");
                return t;
            }

            std::string Format(std::string const& pattern, std::string const& giver)
            {
                return Phrases::Format(pattern, { { "giver", giver } });
            }
            std::string const& Localized(std::vector<std::string> const* strings, int idx, std::string const& base)
            {
                if (strings && idx >= 0 && idx < int(strings->size()) && !(*strings)[idx].empty())
                    return (*strings)[idx];
                return base;
            }

            std::string ItemName(uint32 itemId, int idx)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                std::string const base = proto && proto->Name1 ? proto->Name1 : "";
                ItemLocale const* locale = sObjectMgr.GetItemLocale(itemId);
                return Localized(locale ? &locale->Name : nullptr, idx, base);
            }

            std::string CreatureName(uint32 entry, int idx)
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                std::string const base = info ? info->name : "";
                CreatureLocale const* locale = sObjectMgr.GetCreatureLocale(entry);
                return Localized(locale ? &locale->Name : nullptr, idx, base);
            }

            // The real quest's text in locale idx (-1: base).
            struct RealText
            {
                Quest const* quest;
                QuestLocale const* locale;
                std::string const& Title(int idx) const { return Localized(locale ? &locale->Title : nullptr, idx, quest->GetTitle()); }
                std::string const& Objectives(int idx) const { return Localized(locale ? &locale->Objectives : nullptr, idx, quest->GetObjectives()); }
                std::string const& Details(int idx) const { return Localized(locale ? &locale->Details : nullptr, idx, quest->GetDetails()); }
                // The quest giver's text, as the quest log shows it under "Description".
                std::string Description(int idx) const
                {
                    std::string const& details = Details(idx);
                    return details.empty() ? Objectives(idx) : details;
                }
                std::string const& ObjectiveText(uint32 i, int idx) const
                {
                    return Localized(locale ? &locale->ObjectiveText[i] : nullptr, idx, quest->ObjectiveText[i]);
                }
            };

            // Locale indexes the server loaded (contiguous from 0).
            std::vector<int> LoadedLocaleIndexes()
            {
                std::vector<int> indexes;
                for (int idx = 0; idx < MAX_LOCALE; ++idx)
                {
                    if (sObjectMgr.GetLocaleForIndex(idx) == LOCALE_enUS)
                        break;
                    indexes.push_back(idx);
                }
                return indexes;
            }

            void SetLocale(std::vector<std::string>& strings, int idx, std::string value)
            {
                if (int(strings.size()) <= idx)
                    strings.resize(idx + 1);
                strings[idx] = std::move(value);
            }

            std::unordered_map<uint32, uint32> CreatureGivers()
            {
                std::unordered_map<uint32, uint32> givers;  // quest -> creature entry
                for (auto const& relation : sObjectMgr.GetCreatureQuestRelationsMap())
                    givers.emplace(relation.second, relation.first);
                return givers;
            }

            bool IsMirrorQuest(Quest const* quest)
            {
                return quest && quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_MIRROR);
            }
        }

        std::vector<Counter> CountersOf(Quest const* real)
        {
            std::vector<Counter> counters;
            if (!real)
                return counters;
            for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT && counters.size() < QUEST_OBJECTIVES_COUNT; ++i)
                if (real->ReqCreatureOrGOId[i] && real->ReqCreatureOrGOCount[i])
                    counters.push_back({ Counter::Kind::CreatureOrGO, uint8(i), std::min(real->ReqCreatureOrGOCount[i], MAX_COUNT) });
            for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT && counters.size() < QUEST_OBJECTIVES_COUNT; ++i)
                if (real->ReqItemId[i] && real->ReqItemCount[i])
                    counters.push_back({ Counter::Kind::Item, uint8(i), std::min(real->ReqItemCount[i], MAX_COUNT) });
            if (counters.empty())
                counters.push_back({ Counter::Kind::Done, 0, 1 });
            return counters;
        }

        uint32 Progress(Player const* bot, Quest const* real, Counter const& counter)
        {
            if (!bot || !real)
                return 0;
            QuestStatusData const* status = bot->GetQuestStatusData(real->GetQuestId());
            if (!status)
                return 0;
            if (status->m_status == QUEST_STATUS_COMPLETE || status->m_rewarded)
                return counter.needed;
            switch (counter.kind)
            {
                case Counter::Kind::CreatureOrGO: return std::min(status->m_creatureOrGOcount[counter.index], counter.needed);
                case Counter::Kind::Item:         return std::min(status->m_itemcount[counter.index], counter.needed);
                case Counter::Kind::Done:         return 0;
            }
            return 0;
        }

        uint32 Builder::BuildAll()
        {
            std::vector<int> const locales = LoadedLocaleIndexes();
            std::unordered_map<uint32, uint32> const givers = CreatureGivers();

            // (copied: AddCodeQuest changes the map)
            std::vector<Quest const*> reals;
            for (auto const& itr : sObjectMgr.GetQuestTemplates())
                if (Quest const* quest = itr.second.get())
                    if (quest->GetRequiredClasses() && quest->GetQuestId() < MIRROR_OFFSET)
                        reals.push_back(quest);

            // What a mirror and a placeholder share: no giver, no rewards, no limits, no chain.
            auto blank = [](Quest const& real, uint32 id)
            {
                std::unique_ptr<Quest> mirror(new Quest());
                mirror->QuestId = id;
                mirror->QuestMethod = QUEST_METHOD_DELIVER;  // needs a turn-in nobody can take
                mirror->ZoneOrSort = real.ZoneOrSort;        // the same header in the quest log
                mirror->QuestLevel = real.QuestLevel;
                mirror->Type = real.Type;
                mirror->SuggestedPlayers = real.SuggestedPlayers;
                mirror->m_SpecialFlags = QUEST_SPECIAL_FLAG_MIRROR | QUEST_SPECIAL_FLAG_KILL_OR_CAST;
                for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    mirror->ReqCreatureOrGOId[i] = 0;
                    mirror->ReqCreatureOrGOCount[i] = 0;
                    mirror->ReqSpell[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                {
                    mirror->ReqItemId[i] = 0;
                    mirror->ReqItemCount[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
                {
                    mirror->ReqSourceId[i] = 0;
                    mirror->ReqSourceCount[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
                {
                    mirror->RewChoiceItemId[i] = 0;
                    mirror->RewChoiceItemCount[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                {
                    mirror->RewItemId[i] = 0;
                    mirror->RewItemCount[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
                {
                    mirror->RewRepFaction[i] = 0;
                    mirror->RewRepValue[i] = 0;
                }
                for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
                {
                    mirror->DetailsEmote[i] = 0;
                    mirror->DetailsEmoteDelay[i] = 0;
                    mirror->OfferRewardEmote[i] = 0;
                    mirror->OfferRewardEmoteDelay[i] = 0;
                }
                mirror->SetQuestActiveState(true);
                return mirror;
            };

            uint32 made = 0;
            for (Quest const* real : reals)
            {
                RealText const text{ real, sObjectMgr.GetQuestLocale(real->GetQuestId()) };

                // The quest itself.
                {
                    std::unique_ptr<Quest> mirror = blank(*real, MirrorId(real->GetQuestId()));
                    std::vector<Counter> const counters = CountersOf(real);
                    QuestLocale locale;
                    auto counterText = [&](Counter const& counter, int idx) -> std::string
                    {
                        switch (counter.kind)
                        {
                            case Counter::Kind::CreatureOrGO: return text.ObjectiveText(counter.index, idx); // empty: the client names it
                            case Counter::Kind::Item:         return ItemName(real->ReqItemId[counter.index], idx);
                            case Counter::Kind::Done:         return TextsFor(idx).done;
                        }
                        return "";
                    };
                    for (uint32 i = 0; i < counters.size(); ++i)
                    {
                        Counter const& counter = counters[i];
                        mirror->ReqCreatureOrGOId[i] = counter.kind == Counter::Kind::CreatureOrGO ?
                            real->ReqCreatureOrGOId[counter.index] : STAND_IN_CREATURES[i];
                        mirror->ReqCreatureOrGOCount[i] = counter.needed;
                        mirror->ObjectiveText[i] = counterText(counter, -1);
                        for (int idx : locales)
                            SetLocale(locale.ObjectiveText[i], idx, counterText(counter, idx));
                    }
                    mirror->m_reqCreatureOrGOcount = uint32(counters.size());
                    mirror->Title = TextsFor(-1).prefix + text.Title(-1);
                    mirror->Objectives = text.Objectives(-1);
                    mirror->Details = TextsFor(-1).mirrorIntro + text.Description(-1);
                    for (int idx : locales)
                    {
                        SetLocale(locale.Title, idx, TextsFor(idx).prefix + text.Title(idx));
                        SetLocale(locale.Objectives, idx, text.Objectives(idx));
                        SetLocale(locale.Details, idx, TextsFor(idx).mirrorIntro + text.Description(idx));
                    }
                    if (sObjectMgr.AddCodeQuest(std::move(mirror), std::move(locale)))
                        ++made;
                    else
                        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "[QuestMirror] Quest id %u is taken, no mirror of quest %u.",
                            MirrorId(real->GetQuestId()), real->GetQuestId());
                }

                // Before the bot has it: bring it to the giver.
                {
                    std::unique_ptr<Quest> placeholder = blank(*real, PlaceholderId(real->GetQuestId()));
                    auto const giverItr = givers.find(real->GetQuestId());
                    auto giverName = [&](int idx) -> std::string
                    {
                        std::string name = giverItr != givers.end() ? CreatureName(giverItr->second, idx) : "";
                        return name.empty() ? TextsFor(idx).someGiver : name;
                    };
                    QuestLocale locale;
                    placeholder->ReqCreatureOrGOId[0] = STAND_IN_CREATURES[0];
                    placeholder->ReqCreatureOrGOCount[0] = 1;
                    placeholder->m_reqCreatureOrGOcount = 1;
                    placeholder->ObjectiveText[0] = Format(TextsFor(-1).placeholderGoal, giverName(-1));
                    placeholder->Title = TextsFor(-1).prefix + text.Title(-1);
                    placeholder->Objectives = text.Objectives(-1);
                    placeholder->Details = Format(TextsFor(-1).placeholderIntro, giverName(-1)) + text.Description(-1);
                    for (int idx : locales)
                    {
                        SetLocale(locale.ObjectiveText[0], idx, Format(TextsFor(idx).placeholderGoal, giverName(idx)));
                        SetLocale(locale.Title, idx, TextsFor(idx).prefix + text.Title(idx));
                        SetLocale(locale.Objectives, idx, text.Objectives(idx));
                        SetLocale(locale.Details, idx, Format(TextsFor(idx).placeholderIntro, giverName(idx)) + text.Description(idx));
                    }
                    if (sObjectMgr.AddCodeQuest(std::move(placeholder), std::move(locale)))
                        ++made;
                    else
                        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "[QuestMirror] Quest id %u is taken, no placeholder of quest %u.",
                            PlaceholderId(real->GetQuestId()), real->GetQuestId());
                }
            }
            return made;
        }

        bool Builder::BuildTrip(uint32 id, uint32 questLevel, std::function<TripText(int localeIdx)> const& text)
        {
            std::unique_ptr<Quest> quest(new Quest());
            quest->QuestId = id;
            quest->QuestMethod = QUEST_METHOD_DELIVER;   // nobody turns it in
            quest->ZoneOrSort = 0;
            quest->QuestLevel = int32(questLevel);
            quest->m_SpecialFlags = QUEST_SPECIAL_FLAG_MIRROR | QUEST_SPECIAL_FLAG_KILL_OR_CAST;
            for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                quest->ReqCreatureOrGOId[i] = 0;
                quest->ReqCreatureOrGOCount[i] = 0;
                quest->ReqSpell[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            {
                quest->ReqItemId[i] = 0;
                quest->ReqItemCount[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
            {
                quest->ReqSourceId[i] = 0;
                quest->ReqSourceCount[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            {
                quest->RewChoiceItemId[i] = 0;
                quest->RewChoiceItemCount[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            {
                quest->RewItemId[i] = 0;
                quest->RewItemCount[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
            {
                quest->RewRepFaction[i] = 0;
                quest->RewRepValue[i] = 0;
            }
            for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
            {
                quest->DetailsEmote[i] = 0;
                quest->DetailsEmoteDelay[i] = 0;
                quest->OfferRewardEmote[i] = 0;
                quest->OfferRewardEmoteDelay[i] = 0;
            }
            quest->ReqCreatureOrGOId[0] = STAND_IN_CREATURES[0];
            quest->ReqCreatureOrGOCount[0] = 1;
            quest->m_reqCreatureOrGOcount = 1;

            TripText const base = text(-1);
            quest->Title = TextsFor(-1).prefix + base.title;
            quest->Objectives = base.objective;
            quest->Details = base.details;
            quest->ObjectiveText[0] = base.objective;
            QuestLocale locale;
            for (int idx : LoadedLocaleIndexes())
            {
                TripText const t = text(idx);
                SetLocale(locale.Title, idx, TextsFor(idx).prefix + t.title);
                SetLocale(locale.Objectives, idx, t.objective);
                SetLocale(locale.Details, idx, t.details);
                SetLocale(locale.ObjectiveText[0], idx, t.objective);
            }
            quest->SetQuestActiveState(true);
            if (sObjectMgr.AddCodeQuest(std::move(quest), std::move(locale)))
                return true;
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "[QuestMirror] Quest id %u is taken, no trip quest.", id);
            return false;
        }

        uint32 BuildAll()
        {
            return Builder::BuildAll();
        }

        bool HasMirror(uint32 questId)
        {
            return IsMirrorQuest(sObjectMgr.GetQuestTemplate(MirrorId(questId)));
        }

        ShowResult Show(Player* player, uint32 mirrorId)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(mirrorId);
            if (!player || !quest)
                return ShowResult::NoQuest;
            if (IsShown(player, mirrorId))
                return ShowResult::AlreadyShown;
            if (!player->SatisfyQuestLog(false))
                return ShowResult::LogFull;
            player->AddQuest(quest, nullptr);
            return ShowResult::Shown;
        }

        bool IsShown(Player const* player, uint32 mirrorId)
        {
            return player && player->FindQuestSlot(mirrorId) < MAX_QUEST_LOG_SIZE;
        }

        bool SetCount(Player* player, uint32 mirrorId, uint32 objective, uint32 count)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(mirrorId);
            if (!player || !quest || objective >= QUEST_OBJECTIVES_COUNT || !IsShown(player, mirrorId))
                return false;
            uint32 const needed = quest->ReqCreatureOrGOCount[objective];
            if (!quest->ReqCreatureOrGOId[objective] || !needed)
                return false;

            count = std::min(count, needed);
            QuestStatusData& status = player->GetQuestStatusMap()[mirrorId];
            if (status.m_creatureOrGOcount[objective] == count)
                return true;
            status.m_creatureOrGOcount[objective] = count;
            if (status.uState != QUEST_NEW)
                status.uState = QUEST_CHANGED;
            player->SendQuestUpdateAddCreatureOrGo(quest, ObjectGuid(), objective, count);

            if (status.m_status == QUEST_STATUS_INCOMPLETE && player->CanCompleteQuest(mirrorId))
                player->CompleteQuest(mirrorId);
            else if (status.m_status == QUEST_STATUS_COMPLETE && count < needed)
                player->IncompleteQuest(mirrorId);
            return true;
        }

        bool Remove(Player* player, uint32 mirrorId)
        {
            if (!player)
                return false;
            uint16 const slot = player->FindQuestSlot(mirrorId);
            if (slot >= MAX_QUEST_LOG_SIZE)
                return false;
            player->RemoveQuestAtSlot(slot);
            return true;
        }

        void ShowPoint(Player* player, float x, float y, std::string const& name)
        {
            if (!player || !player->GetSession() || !player->PlayerTalkClass)
                return;
            // Icon and flags of the guards' directions (points_of_interest).
            uint32 const ICON_RED_FLAG = 6;
            uint32 const FLAGS = 99;
            std::string const label = TextsFor(player->GetSession()->GetSessionDbLocaleIndex()).prefix + name;
            player->PlayerTalkClass->SendPointOfInterest(x, y, ICON_RED_FLAG, FLAGS, 0, label.c_str());
        }
    }
}
