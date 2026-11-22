#include "Commander/Commands/Voice/PartyVoice.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Shared/BotChat.h"
#include "Shared/GameNames.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cctype>

namespace Commander
{
    namespace
    {
        namespace Phrases = BotShared::Phrases;
        namespace I18n = BotShared::I18n;

        // How long a line may wait for its turn.
        uint32 ExpiryMs(VoiceTier tier)
        {
            switch (tier)
            {
                case VoiceTier::Urgent:    return 3000;
                case VoiceTier::Essential: return 6000;
                case VoiceTier::Social:    return 30000;
                default:                   return 15000;
            }
        }

        bool HasTag(std::vector<std::string> const& tags, char const* tag)
        {
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }

        // The human's talkativeness and the speaker's personality (PartyVoice.h).
        bool Allowed(VoiceTier tier, VoiceLevel level, std::vector<std::string> const& tags)
        {
            bool const quiet = HasTag(tags, "quiet");
            bool const chatty = HasTag(tags, "chatty");
            switch (tier)
            {
                case VoiceTier::Urgent:
                case VoiceTier::Essential:
                    return true;
                case VoiceTier::Social:
                    return level != VoiceLevel::Quiet && !(level == VoiceLevel::Normal && quiet);
                default:
                    if (level == VoiceLevel::Quiet)
                        return false;
                    if (level == VoiceLevel::Normal)
                        return chatty || (!quiet && urand(0, 99) < 35);
                    return !quiet || urand(0, 99) < 40;
            }
        }

        TeamBotAI* BotAI(Player* p)
        {
            return p && p->IsInWorld() && p->AI() ? dynamic_cast<TeamBotAI*>(p->AI()) : nullptr;
        }

        // "Polymorph: Pig" -> "polymorph", "Shackle Undead" -> "shackle_undead".
        std::string SpellKey(char const* name)
        {
            std::string key;
            for (char const* c = name; c && *c && *c != ':'; ++c)
            {
                unsigned char const ch = static_cast<unsigned char>(*c);
                if (std::isalnum(ch))
                    key += char(std::tolower(ch));
                else if (!key.empty() && key.back() != '_')
                    key += '_';
            }
            while (!key.empty() && key.back() == '_')
                key.pop_back();
            return key;
        }
    }

    void PartyVoice::Queue(VoiceEvent event, uint32 nowMs)
    {
        if (event.key.empty())
            return;
        std::string const once = event.once.empty() ? event.key : event.once;
        auto const itr = m_said.find(once);
        if (itr != m_said.end() && WorldTimer::getMSTimeDiff(itr->second, nowMs) < event.onceMs)
            return;
        m_said[once] = nowMs;
        if (m_said.size() > 256)
        {
            for (auto it = m_said.begin(); it != m_said.end();)
                it = WorldTimer::getMSTimeDiff(it->second, nowMs) > 10 * MINUTE * IN_MILLISECONDS ? m_said.erase(it) : std::next(it);
        }

        if (m_pending.size() >= MAX_PENDING)
        {
            // Make room: the least important, oldest line goes.
            auto worst = std::max_element(m_pending.begin(), m_pending.end(), [](Pending const& a, Pending const& b)
            {
                return a.event.tier != b.event.tier ? a.event.tier < b.event.tier : a.queuedMs > b.queuedMs;
            });
            if (worst->event.tier < event.tier)
                return;
            m_pending.erase(worst);
        }
        Pending p;
        p.event = std::move(event);
        p.queuedMs = nowMs;
        m_pending.push_back(std::move(p));
    }

    void PartyVoice::Say(std::string key, ObjectGuid speaker, VoiceTier tier, uint32 nowMs,
                         std::vector<std::pair<std::string, std::string>> vars, std::string once, uint32 onceMs)
    {
        VoiceEvent e;
        e.key = std::move(key);
        e.speaker = speaker;
        e.tier = tier;
        e.vars = std::move(vars);
        e.once = std::move(once);
        e.onceMs = onceMs;
        Queue(std::move(e), nowMs);
    }

    void PartyVoice::Step(TeamBotAI* bot, WorldModel const& world, TeamPlan& plan, uint32 nowMs)
    {
        std::vector<VoiceEvent> asked;
        asked.swap(plan.voice);
        Map* map = bot->me->GetMap();

        // The human whose settings count: the leader when he is one, else the first one.
        ObjectGuid humanGuid;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isBot && !m.isPet && (humanGuid.IsEmpty() || m.guid == world.leader))
                humanGuid = m.guid;
        if (!world.humanInTeam || humanGuid.IsEmpty())
        {
            // Nobody to tell (a team of bots, the human on another map): keep the facts current.
            m_pending.clear();
            Remember(world, plan);
            return;
        }
        Player const* human = map->GetPlayer(humanGuid);
        VoiceSettings const settings = sCommanderMgr.GetVoiceSettings(humanGuid);

        for (VoiceEvent& e : asked)
            Queue(std::move(e), nowMs);
        // Before the moments: the opening control's line names the mark.
        m_marks.Step(bot->me, world, plan, settings.marks, nowMs);
        FindMoments(map, world, plan, human, settings, nowMs);
        Deliver(map, world, settings, world.HasPartyEnemy(), nowMs);
    }

    void PartyVoice::Deliver(Map* map, WorldModel const& world, VoiceSettings const& settings, bool fighting, uint32 nowMs)
    {
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [nowMs](Pending const& p)
        {
            return WorldTimer::getMSTimeDiff(p.queuedMs, nowMs) > ExpiryMs(p.event.tier);
        }), m_pending.end());
        if (m_pending.empty())
            return;

        // Most important first, oldest first.
        std::stable_sort(m_pending.begin(), m_pending.end(), [](Pending const& a, Pending const& b)
        {
            return a.event.tier != b.event.tier ? a.event.tier < b.event.tier : a.queuedMs < b.queuedMs;
        });
        uint32 const since = m_lastLineMs ? WorldTimer::getMSTimeDiff(m_lastLineMs, nowMs) : UINT32_MAX;
        for (auto itr = m_pending.begin(); itr != m_pending.end();)
        {
            VoiceTier const tier = itr->event.tier;
            bool const turn =
                tier == VoiceTier::Urgent ? since >= URGENT_GAP_MS :
                tier == VoiceTier::Essential ? since >= (fighting ? GAP_FIGHT_MS : GAP_MS) :
                !fighting && since >= FLAVOR_GAP_MS;
            if (!turn)
            {
                ++itr;
                continue;
            }
            Player* speaker = PickSpeaker(map, world, *itr);
            TeamBotAI* ai = BotAI(speaker);
            if (!ai || !Allowed(tier, settings.level, ai->PersonalityTags()))
            {
                itr = m_pending.erase(itr);
                continue;
            }
            Phrases::Vars vars(itr->event.vars.begin(), itr->event.vars.end());
            std::string const text = Phrases::Say(I18n::OfParty(speaker), itr->event.key, ai->PersonalityTags(), vars);
            m_pending.erase(itr);
            if (!text.empty() && BotShared::BotChat::SayToParty(speaker, text))
                m_lastLineMs = nowMs;
            return;     // one line per refresh
        }
    }

    Player* PartyVoice::PickSpeaker(Map* map, WorldModel const& world, Pending const& line) const
    {
        VoiceEvent const& e = line.event;
        if (!e.speaker.IsEmpty())
        {
            Player* p = map->GetPlayer(e.speaker);
            PartyMemberFacts const* m = world.FindMember(e.speaker);
            return m && m->isBot && BotAI(p) ? p : nullptr;
        }

        bool const fightLine = e.tier == VoiceTier::Urgent || e.tier == VoiceTier::Essential;
        if (fightLine)
        {
            // The main tank bot while it is up, else any bot that is.
            if (PartyMemberFacts const* tank = Tanks::Main(world, true))
                if (tank->alive)
                    if (Player* p = map->GetPlayer(tank->guid))
                        if (BotAI(p))
                            return p;
        }

        // A bot that is up; social and atmosphere lines prefer chatty ones, then not quiet ones.
        std::vector<Player*> chatty, plain, quiet;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.alive)
                continue;
            Player* p = map->GetPlayer(m.guid);
            TeamBotAI* ai = BotAI(p);
            if (!ai)
                continue;
            std::vector<std::string> const& tags = ai->PersonalityTags();
            (HasTag(tags, "chatty") ? chatty : HasTag(tags, "quiet") ? quiet : plain).push_back(p);
        }
        for (std::vector<Player*> const* group : { &chatty, &plain, &quiet })
            if (!group->empty())
                return (*group)[urand(0, uint32(group->size()) - 1)];
        return nullptr;
    }

    std::string PartyVoice::EnemyName(Map* map, ObjectGuid enemy, Player const* human) const
    {
        Unit const* unit = map->GetUnit(enemy);
        std::string const name = unit ? BotShared::GameNames::UnitName(unit, human) : std::string("?");
        int8 const icon = m_marks.IconOf(enemy);
        if (icon == ControlMarks::NONE)
            return name;
        I18n::Lang const lang = human ? I18n::Of(human) : I18n::Default();
        std::string const mark = Phrases::Word(lang, "voice.mark." + std::to_string(int(icon)));
        return Phrases::Text(lang, "voice.marked", { { "mark", mark }, { "name", name } });
    }

    std::string PartyVoice::MemberName(Map* map, ObjectGuid member) const
    {
        Player const* p = map->GetPlayer(member);
        return p ? std::string(p->GetName()) : std::string("?");
    }

    std::string PartyVoice::ControlWord(Player const* human, uint32 spellId) const
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            return std::string();
        std::string const english = spell->SpellName[0];
        I18n::Lang const lang = human ? I18n::Of(human) : I18n::Default();
        return Phrases::Word(lang, "voice.cc." + SpellKey(english.c_str()), english);
    }
}
