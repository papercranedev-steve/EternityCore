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
            { "errand.abort",    "default",     "你们要走了？等等我！" },
            { "reward.ask",      "default",     "{quest}的奖励我拿哪个好？" },
            { "reward.ask",      "frugal",      "{quest}的奖励，挑个最值钱的吧？你帮我看看。" },
            // Words and button labels (tag "default" only).
            { "place.vendor",    "default",     "商人" },
            { "place.trainer",   "default",     "训练师" },
            { "place.and",       "default",     "和" },
            { "errand.opt.yes",  "default",     "去吧" },
            { "errand.opt.no",   "default",     "先别去" },
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
