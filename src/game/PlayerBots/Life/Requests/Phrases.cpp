#include "Life/Requests/Phrases.h"
#include "Database/DatabaseEnv.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <set>

namespace Life
{
namespace Phrases
{
    namespace
    {
        struct Line
        {
            char const* key;
            char const* tag;
            char const* text;
        };

        // Seeded into the table once. Errand variables: {places} ("商人和训练师"), {gold}
        // (what the trainer costs, may be empty), {leader}. Reward: {quest}.
        Line const DEFAULTS[] = {
            { "errand.ask",      "default",     "我想去{places}那里一趟，能等我一下吗？" },
            { "errand.ask",      "default",     "{leader}，我去趟{places}，很快回来，行吗？" },
            { "errand.ask",      "frugal",      "{places}那里要花{gold}，我攒了好久了，能等我一下吗？" },
            { "errand.ask",      "spendthrift", "钱又快花光了……我去{places}那边一下，等我？" },
            { "errand.ask",      "chatty",      "对了对了，我得去{places}那里一趟，补点东西，顺便学点新本事，等我一下好不好？" },
            { "errand.ask",      "quiet",       "去{places}，马上回。" },
            { "errand.ask",      "lazy",        "唉，又得跑一趟{places}……等我会儿？" },
            { "errand.go",       "default",     "好，我去去就回。" },
            { "errand.go",       "diligent",    "马上就好！" },
            { "errand.go",       "quiet",       "嗯。" },
            { "errand.declined", "default",     "好吧，那下次再说。" },
            { "errand.declined", "lazy",        "正好，我也懒得跑。" },
            { "errand.back",     "default",     "我回来了。" },
            { "errand.back",     "chatty",      "回来啦，东西都买齐了！" },
            // After the trip, the leader out of sight (whispered). {gate}: the gate's name.
            { "meet.ask",        "default",     "我这边办完了。你在哪？我去找你，还是在{gate}等你？" },
            { "meet.ask",        "chatty",      "东西都买好啦！你还在忙吗？要我过去找你，还是我去{gate}等你？" },
            { "meet.ask",        "quiet",       "好了。找你，还是{gate}等？" },
            { "meet.come",       "default",     "好，我这就过去。" },
            { "meet.gate",       "default",     "好，我去{gate}等你。" },
            { "meet.here",       "default",     "好，我就在这儿等你。" },
            { "meet.arrived",    "default",     "我到{gate}了，你忙完过来就行。" },
            { "meet.arrived",    "lazy",        "我在{gate}坐着呢，不急。" },
            { "meet.opt.come",   "default",     "来找我" },
            { "meet.opt.gate",   "default",     "去{gate}等我" },
            { "meet.opt.here",   "default",     "原地等我" },
            { "place.gate",      "default",     "城门口" },
            { "place.here",      "default",     "原地" },
            { "reward.ask",      "default",     "{quest}的奖励我拿哪个好？" },
            { "reward.ask",      "frugal",      "{quest}的奖励，挑个最值钱的吧？你帮我看看。" },
            // Class quests (Companions/ClassQuests/ClassQuestFlow). {quest}: the chain's last quest,
            // {leader}; thanks: {affinity} (now, of 100).
            { "classquest.ask",      "default", "{leader}，我到等级了，能带我去做职业任务「{quest}」吗？" },
            { "classquest.ask",      "chatty",  "{leader}{leader}！我听说「{quest}」能学到新本事，你陪我去一趟好不好？" },
            { "classquest.ask",      "quiet",   "「{quest}」，能带我去吗？" },
            { "classquest.ask",      "lazy",    "那个「{quest}」……一个人去太累了，你带带我？" },
            { "classquest.askgear",  "default", "{leader}，有个职业任务「{quest}」能拿件装备，有空陪我去吗？" },
            { "classquest.accepted", "default", "太好了！任务我写给你看了，照着走就行。" },
            { "classquest.accepted", "quiet",   "谢了。" },
            { "classquest.declined", "default", "好吧，那下次进城再说。" },
            { "classquest.giveup",   "default", "算了，我以后自己想办法吧。" },
            { "classquest.giveup",   "lazy",    "那就先不做了，反正也不急。" },
            { "classquest.dropped",  "default", "那这个任务我先放着了。" },
            { "classquest.step",     "default", "这一步完成了，下一步看任务日志。" },
            { "classquest.thanks",   "default", "做完了！多亏有你，谢谢！" },
            { "classquest.thanks",   "chatty",  "成啦成啦！没有你我肯定搞不定，太谢谢了！" },
            { "classquest.thanks",   "quiet",   "成了。谢谢。" },
            { "classquest.selfdone", "default", "对了，「{quest}」我自己去做完了。" },
            { "classquest.selfdone", "lazy",    "那个「{quest}」……拖了好久，总算自己做完了。" },
            { "questitem.tame",      "default", "我在驯服它，别打它！" },
            // Conjured supplies (Life/Errands/GiveProvisions, AskProvisions). {leader}; {what}:
            // supply.what.* ("水和面包"); {names}: the bots that ask; {giver}: the mage asked.
            { "supply.offer",        "default", "{leader}，你的{what}不多了，要我给你做点吗？" },
            { "supply.offer",        "chatty",  "{leader}，我看你{what}快没了，我顺手给你变一些吧？" },
            { "supply.offer",        "quiet",   "{what}，要吗？" },
            { "supply.offer.stone",  "default", "{leader}，你身上没有治疗石，要我给你做一块吗？" },
            { "supply.offer.stone",  "quiet",   "治疗石，要吗？" },
            { "supply.declined",     "default", "好，需要的时候跟我说。" },
            { "supply.make",         "default", "稍等，我现做一些。" },
            { "supply.give",         "default", "拿好。" },
            { "supply.give",         "chatty",  "刚做好的，拿去吧！" },
            { "supply.noroom",       "default", "你的包满了，腾点地方我再给你。" },
            { "supply.failed",       "default", "交易没成，等会儿再说吧。" },
            { "supply.askmage",      "default", "{leader}，我的{what}快没了，能帮我做点吗？" },
            { "supply.askmage",      "lazy",    "{leader}，{what}没了……能给我变点吗？" },
            { "supply.askmage.many", "default", "{leader}，{names}的{what}都快没了，能帮我们做点吗？" },
            { "supply.botask",       "default", "{giver}，能给我点{what}吗？" },
            { "supply.botask",       "quiet",   "{giver}，{what}。" },
            { "supply.botack",       "default", "好，稍等。" },
            { "supply.thanks",       "default", "谢了！" },
            { "supply.thanks",       "quiet",   "谢。" },
            { "supply.what.both",    "default", "水和面包" },
            { "supply.what.food",    "default", "面包" },
            { "supply.what.drink",   "default", "水" },
            { "supply.opt.yes",      "default", "要" },
            { "supply.opt.no",       "default", "不用了" },
            { "supply.opt.make",     "default", "给你们做" },
            { "supply.opt.later",    "default", "现在不行" },
            { "classquest.opt.yes",  "default", "带你去" },
            { "classquest.opt.no",   "default", "现在不行" },
            // Words and button labels (tag "default" only).
            { "place.vendor",    "default",     "商人" },
            { "place.vendor.repair",  "default", "护甲商" },
            { "place.vendor.ammo",    "default", "弹药商" },
            { "place.vendor.food",    "default", "食物商人" },
            { "place.vendor.reagent", "default", "材料商" },
            { "place.auction",   "default",     "拍卖行" },
            { "place.trainer",   "default",     "训练师" },
            { "place.and",       "default",     "和" },
            { "place.comma",     "default",     "、" },
            { "errand.opt.yes",  "default",     "去吧" },
            { "errand.opt.no",   "default",     "先别去" },
            // Short of money for the trainer (Trip/TrainingPlanner): three answers, the money
            // line under the lessons. {important}, {all}, {have}: money.
            { "errand.opt.pay",  "default",     "给钱去学" },
            { "errand.opt.nopay", "default",    "不给钱去学" },
            { "errand.paid",     "default",     "谢谢！我这就去。" },
            { "errand.paid",     "frugal",      "谢啦，这钱我一定花在刀刃上。" },
            { "training.money",  "default",     "学费：重要技能{important}，全部{all}，我身上有{have}。" },
            // Out in the field (Life/Errands/LessonReminder). {lessons}: the names.
            { "lesson.remind",   "default",     "我现在能学{lessons}了，下次进主城记得带我去趟训练师。" },
            { "lesson.remind",   "chatty",      "对了对了！我能学{lessons}了，进主城的时候陪我去趟训练师好不好？" },
            { "lesson.remind",   "quiet",       "能学{lessons}了。进城时带我去训练师。" },
            { "lesson.remind.again", "default", "{lessons}我还没学呢，进主城的时候别忘了带我去训练师。" },
            { "money.gold",      "default",     "金" },
            { "money.silver",    "default",     "银" },
            { "money.copper",    "default",     "铜" },
        };

        struct Template
        {
            std::string tag;
            std::string text;
        };
        std::map<std::string, std::vector<Template>> s_byKey;

        bool EnsureTable()
        {
            CharacterDatabase.DirectExecute(
                "CREATE TABLE IF NOT EXISTS `custom_companion_phrase` ("
                " `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
                " `key` VARCHAR(32) NOT NULL COMMENT '<intent>.<moment>, e.g. errand.ask',"
                " `tag` VARCHAR(24) NOT NULL DEFAULT 'default' COMMENT 'personality tag key or default',"
                " `text` VARCHAR(255) NOT NULL COMMENT '{name} variables',"
                " PRIMARY KEY (`id`), KEY `idx_key` (`key`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: what they say (soul companions doc 5.1)'");
            std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_companion_phrase'"));
            return exists != nullptr;
        }

        // Defaults of keys the table has no row of (a key added by newer code; the user's own
        // edits of a key are kept).
        void Seed()
        {
            std::set<std::string> present;
            std::unique_ptr<QueryResult> keys(CharacterDatabase.Query("SELECT DISTINCT `key` FROM `custom_companion_phrase`"));
            if (keys)
            {
                do
                {
                    present.insert(keys->Fetch()[0].GetCppString());
                } while (keys->NextRow());
            }
            for (Line const& line : DEFAULTS)
            {
                if (present.count(line.key))
                    continue;
                std::string key = line.key, tag = line.tag, text = line.text;
                CharacterDatabase.escape_string(key);
                CharacterDatabase.escape_string(tag);
                CharacterDatabase.escape_string(text);
                CharacterDatabase.DirectPExecute("INSERT INTO `custom_companion_phrase` (`key`, `tag`, `text`) VALUES ('%s', '%s', '%s')",
                    key.c_str(), tag.c_str(), text.c_str());
            }
        }

        void UseDefaults()
        {
            for (Line const& line : DEFAULTS)
                s_byKey[line.key].push_back({ line.tag, line.text });
        }
    }

    void Load()
    {
        s_byKey.clear();
        if (!EnsureTable())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Phrases] No table `custom_companion_phrase`: built-in phrases only.");
            UseDefaults();
            return;
        }
        Seed();
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT `key`, `tag`, `text` FROM `custom_companion_phrase`"));
        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                s_byKey[f[0].GetCppString()].push_back({ f[1].GetCppString(), f[2].GetCppString() });
                ++count;
            } while (result->NextRow());
        }
        if (!count)
            UseDefaults();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [Phrases] %u companion phrases loaded.", count);
    }

    std::string Pick(std::string const& key, std::vector<std::string> const& tags)
    {
        auto itr = s_byKey.find(key);
        if (itr == s_byKey.end())
            return std::string();
        std::vector<Template const*> tagged, plain;
        for (Template const& t : itr->second)
        {
            if (t.tag == "default")
                plain.push_back(&t);
            else
                for (std::string const& tag : tags)
                    if (t.tag == tag)
                        tagged.push_back(&t);
        }
        std::vector<Template const*> const& from = tagged.empty() ? plain : tagged;
        if (from.empty())
            return std::string();
        return from[urand(0, uint32(from.size()) - 1)]->text;
    }

    std::string Format(std::string const& text, Vars const& vars)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();)
        {
            if (text[i] == '{')
            {
                size_t const end = text.find('}', i + 1);
                if (end != std::string::npos)
                {
                    auto var = vars.find(text.substr(i + 1, end - i - 1));
                    if (var != vars.end())
                    {
                        out += var->second;
                        i = end + 1;
                        continue;
                    }
                }
            }
            out += text[i++];
        }
        return out;
    }

    std::string Say(std::string const& key, std::vector<std::string> const& tags, Vars const& vars)
    {
        return Format(Pick(key, tags), vars);
    }

    std::string Word(std::string const& key, std::string const& fallback)
    {
        std::string const word = Pick(key, {});
        return word.empty() ? fallback : word;
    }

    std::string Money(uint32 copper)
    {
        uint32 const gold = copper / 10000;
        uint32 const silver = copper / 100 % 100;
        uint32 const rest = copper % 100;
        std::string out;
        if (gold)
            out += std::to_string(gold) + Word("money.gold", "g");
        if (silver)
            out += std::to_string(silver) + Word("money.silver", "s");
        if (rest || out.empty())
            out += std::to_string(rest) + Word("money.copper", "c");
        return out;
    }
}
}
