#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Shared/Phrases.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        char const* const BACK_ARG = "back";

        std::pair<std::string, std::string> Split(std::string const& id)
        {
            size_t const colon = id.find(':');
            if (colon == std::string::npos)
                return { id, std::string() };
            return { id.substr(0, colon), id.substr(colon + 1) };
        }

        // A sub-page of options (user 2026-11-22: 互动, 队伍): on the first page one line with
        // the folder icon, offered while one of its services offers something; picked, a page
        // of their options and "说点别的" back to the first page. Its services' option ids go
        // out as "<folder>:<their id>", so Talk::Choose finds the folder and the folder its
        // service, which checks its own arguments as on the first page.
        class FolderService : public Service
        {
        public:
            FolderService(char const* key, char const* optionWord, char const* pageLine,
                          std::vector<std::unique_ptr<Service>> services)
                : m_key(key), m_optionWord(optionWord), m_pageLine(pageLine), m_services(std::move(services)) {}

            char const* Key() const override { return m_key; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (Inside(ctx).empty())
                    return;
                Option option;
                option.id = m_key;
                option.icon = Icon::Folder;
                option.text = BotShared::Phrases::Word(ctx.lang, m_optionWord);
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                outcome.kind = Outcome::Kind::Page;
                if (arg == BACK_ARG)
                {
                    outcome.text = Line(ctx, "talk.back.page");
                    outcome.options = Offers(ctx);
                    return outcome;
                }
                if (arg.empty())
                    return Page(ctx, Line(ctx, m_pageLine));

                std::pair<std::string, std::string> const parts = Split(arg);
                Service const* service = nullptr;
                for (std::unique_ptr<Service> const& s : m_services)
                    if (parts.first == s->Key())
                        service = s.get();
                bool offered = false;
                if (service)
                {
                    std::vector<Option> options;
                    service->Offer(ctx, options);
                    offered = !options.empty();
                }
                // (gone since the page was drawn: the page as it is now)
                if (!offered)
                    return Page(ctx, Line(ctx, "talk.gone"));

                outcome = service->Choose(ctx, parts.second);
                for (Option& option : outcome.options)
                    option.id = std::string(m_key) + ":" + option.id;
                return outcome;
            }

            void Blockers(Player* bot, BotShared::I18n::Lang lang, std::vector<Blocker>& out) const override
            {
                for (std::unique_ptr<Service> const& service : m_services)
                    service->Blockers(bot, lang, out);
            }

        private:
            // Its services' options now, with the folder's key in front.
            std::vector<Option> Inside(Context const& ctx) const
            {
                std::vector<Option> options;
                for (std::unique_ptr<Service> const& service : m_services)
                    service->Offer(ctx, options);
                for (Option& option : options)
                    option.id = std::string(m_key) + ":" + option.id;
                return options;
            }

            Outcome Page(Context const& ctx, std::string const& text) const
            {
                Outcome outcome;
                outcome.kind = Outcome::Kind::Page;
                outcome.text = text;
                outcome.options = Inside(ctx);
                Option back;
                back.id = std::string(m_key) + ":" + BACK_ARG;
                back.icon = Icon::Chat;
                back.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.back");
                outcome.options.push_back(back);
                return outcome;
            }

            char const* m_key;
            char const* m_optionWord;
            char const* m_pageLine;
            std::vector<std::unique_ptr<Service>> m_services;
        };
    }

    std::unique_ptr<Service> MakeFolderService(char const* key, char const* optionWord, char const* pageLine,
                                               std::vector<std::unique_ptr<Service>> services)
    {
        return std::make_unique<FolderService>(key, optionWord, pageLine, std::move(services));
    }
}
}
