#include "Shared/Phrases.h"
#include "Config/Config.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>

namespace BotShared
{
namespace Phrases
{
    namespace
    {
        struct Template
        {
            std::string tag;
            std::string text;
        };
        using Table = std::map<std::string, std::vector<Template>>;
        struct Tables
        {
            Table byLang[I18n::COUNT];
        };

        std::mutex s_lock;
        std::shared_ptr<Tables const> s_tables = std::make_shared<Tables>();
        std::set<std::string> s_missing;     // keys logged as missing (once each)
        std::string s_dir = "i18n";

        std::shared_ptr<Tables const> Current()
        {
            std::lock_guard<std::mutex> guard(s_lock);
            return s_tables;
        }

        Lang Other(Lang lang)
        {
            return lang == Lang::Zh ? Lang::En : Lang::Zh;
        }

        void AddLines(Table& table, std::string const& key, std::string const& tag, nlohmann::json const& value)
        {
            if (value.is_string())
                table[key].push_back({ tag, value.get<std::string>() });
            else if (value.is_array())
                for (nlohmann::json const& line : value)
                    if (line.is_string())
                        table[key].push_back({ tag, line.get<std::string>() });
        }

        // One language's file into `table`; the number of lines, -1 when it cannot be read.
        int32 ReadFile(Lang lang, Table& table)
        {
            std::string const path = s_dir + "/" + I18n::Code(lang) + ".json";
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Phrases] Cannot open %s (PartyBot.I18nDir).", path.c_str());
                return -1;
            }
            nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Phrases] %s is not a JSON object (contrib/i18n/i18n.mjs check).", path.c_str());
                return -1;
            }
            int32 count = 0;
            for (auto itr = root.begin(); itr != root.end(); ++itr)
            {
                std::string const& key = itr.key();
                if (key.compare(0, 2, "//") == 0)
                    continue;
                if (itr.value().is_object())
                    for (auto tag = itr.value().begin(); tag != itr.value().end(); ++tag)
                        AddLines(table, key, tag.key(), tag.value());
                else
                    AddLines(table, key, "default", itr.value());
                count += int32(table[key].size());
            }
            return count;
        }

        // Empty: the language has no line of the key.
        std::string PickIn(Tables const& tables, Lang lang, std::string const& key, std::vector<std::string> const& tags)
        {
            Table const& table = tables.byLang[uint8(lang)];
            auto itr = table.find(key);
            if (itr == table.end())
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

        void LogMissing(std::string const& key)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            if (s_missing.insert(key).second)
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Phrases] No text for '%s' in any language.", key.c_str());
        }
    }

    void LoadConfig()
    {
        s_dir = sConfig.GetStringDefault("PartyBot.I18nDir", "i18n");
    }

    bool Load()
    {
        auto tables = std::make_shared<Tables>();
        int32 count[I18n::COUNT];
        bool ok = true;
        for (uint8 i = 0; i < I18n::COUNT; ++i)
        {
            count[i] = ReadFile(Lang(i), tables->byLang[i]);
            ok = ok && count[i] >= 0;
        }
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_tables = tables;
            s_missing.clear();
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [Phrases] %d Chinese and %d English lines loaded from %s.",
            count[uint8(Lang::Zh)], count[uint8(Lang::En)], s_dir.c_str());
        return ok;
    }

    std::string Pick(Lang lang, std::string const& key, std::vector<std::string> const& tags)
    {
        std::shared_ptr<Tables const> const tables = Current();
        std::string const text = PickIn(*tables, lang, key, tags);
        return text.empty() ? PickIn(*tables, Other(lang), key, tags) : text;
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

    std::string Say(Lang lang, std::string const& key, std::vector<std::string> const& tags, Vars const& vars)
    {
        std::string const text = Pick(lang, key, tags);
        if (text.empty())
            LogMissing(key);
        return Format(text, vars);
    }

    std::string Word(Lang lang, std::string const& key)
    {
        std::string const word = Pick(lang, key, {});
        if (word.empty())
            LogMissing(key);
        return word;
    }

    std::string Word(Lang lang, std::string const& key, std::string const& fallback)
    {
        std::string const word = Pick(lang, key, {});
        return word.empty() ? fallback : word;
    }

    std::string Text(Lang lang, std::string const& key, Vars const& vars)
    {
        return Format(Word(lang, key), vars);
    }

    std::string Money(Lang lang, uint32 copper)
    {
        uint32 const gold = copper / 10000;
        uint32 const silver = copper / 100 % 100;
        uint32 const rest = copper % 100;
        std::string out;
        if (gold)
            out += std::to_string(gold) + Word(lang, "money.gold");
        if (silver)
            out += std::to_string(silver) + Word(lang, "money.silver");
        if (rest || out.empty())
            out += std::to_string(rest) + Word(lang, "money.copper");
        return out;
    }
}
}
