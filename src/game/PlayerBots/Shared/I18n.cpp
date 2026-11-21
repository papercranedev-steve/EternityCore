#include "Shared/I18n.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "Group.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include <mutex>
#include <unordered_map>

namespace BotShared
{
namespace I18n
{
    namespace
    {
        std::mutex s_lock;
        std::unordered_map<uint32, Lang> s_accounts;
        Lang s_default = Lang::En;

        bool IsHuman(Player const* p)
        {
            return p && p->GetSession() && !p->IsBot();
        }

        Lang OfHuman(Player const* p)
        {
            WorldSession const* session = p->GetSession();
            {
                std::lock_guard<std::mutex> guard(s_lock);
                auto const itr = s_accounts.find(session->GetAccountId());
                if (itr != s_accounts.end())
                    return itr->second;
            }
            // (no zhCN dbc on the server: the dbc locale falls back to enUS, the database one need not)
            if (OfLocale(session->GetSessionDbcLocale()) == Lang::Zh)
                return Lang::Zh;
            int const idx = session->GetSessionDbLocaleIndex();
            return idx >= 0 ? OfLocale(sObjectMgr.GetLocaleForIndex(idx)) : Lang::En;
        }
    }

    char const* Code(Lang lang)
    {
        return lang == Lang::Zh ? "zh" : "en";
    }

    bool Parse(std::string const& code, Lang& out)
    {
        if (code == "zh")
            out = Lang::Zh;
        else if (code == "en")
            out = Lang::En;
        else
            return false;
        return true;
    }

    void LoadConfig()
    {
        std::string const code = sConfig.GetStringDefault("PartyBot.Language", "en");
        if (!Parse(code, s_default))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[I18n] PartyBot.Language '%s' is not zh or en, using en.", code.c_str());
            s_default = Lang::En;
        }
    }

    void Load()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_account_language` ("
            " `account` INT UNSIGNED NOT NULL,"
            " `lang` VARCHAR(4) NOT NULL COMMENT 'zh | en',"
            " PRIMARY KEY (`account`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='PartyBot: the language an account reads (the addon''s switch)'");

        std::unordered_map<uint32, Lang> accounts;
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT `account`, `lang` FROM `custom_account_language`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                Lang lang;
                if (Parse(f[1].GetCppString(), lang))
                    accounts[f[0].GetUInt32()] = lang;
            } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(s_lock);
        s_accounts = std::move(accounts);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [I18n] %u account languages loaded, default %s.",
            uint32(s_accounts.size()), Code(s_default));
    }

    Lang Default()
    {
        return s_default;
    }

    Lang OfLocale(LocaleConstant locale)
    {
        return locale == LOCALE_zhCN || locale == LOCALE_zhTW ? Lang::Zh : Lang::En;
    }

    Lang Of(Player const* reader)
    {
        return IsHuman(reader) ? OfHuman(reader) : OfParty(reader);
    }

    Lang OfParty(Player const* member)
    {
        if (!member)
            return Default();
        if (IsHuman(member))
            return OfHuman(member);
        Group* group = const_cast<Player*>(member)->GetGroup();
        if (!group)
            return Default();
        Player const* first = nullptr;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player const* p = itr->getSource();
            if (!IsHuman(p))
                continue;
            if (p->GetObjectGuid() == group->GetLeaderGuid())
                return OfHuman(p);
            if (!first)
                first = p;
        }
        return first ? OfHuman(first) : Default();
    }

    void Set(uint32 accountId, Lang lang)
    {
        {
            std::lock_guard<std::mutex> guard(s_lock);
            s_accounts[accountId] = lang;
        }
        CharacterDatabase.PExecute("REPLACE INTO `custom_account_language` (`account`, `lang`) VALUES (%u, '%s')",
            accountId, Code(lang));
    }
}
}
