#include "Companions/Slots/Slots.h"
#include "Config/Config.h"
#include "World.h"
#include "Log.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace Companions
{
    namespace Slots
    {
        namespace
        {
            // A price from slot `from` on (until the next step), in gold.
            struct PriceStep
            {
                uint32 from = 0;
                uint32 gold = 0;
            };

            // Slots 1-5 by the first dungeons and the bench, 6-8 one per class of a faction
            // (each can later join the account), slots 9+ bought (slots doc §2).
            char const* const DEFAULT_FREE_LEVELS = "6 10 13 16 30 40 50 60";
            // Gold at Rate.Drop.Money = 1 (PriceOf scales them: what a level 60 earns follows it).
            char const* const DEFAULT_PRICES = "9:150 13:250 20:400";
            uint32 const DEFAULT_MAX = 80;

            std::vector<uint32> g_freeLevels;   // ascending
            std::vector<PriceStep> g_prices;    // ascending by `from`
            uint32 g_max = DEFAULT_MAX;

            std::vector<uint32> ParseLevels(std::string const& text)
            {
                std::vector<uint32> levels;
                std::istringstream in(text);
                uint32 level = 0;
                while (in >> level)
                    if (level)
                        levels.push_back(level);
                std::sort(levels.begin(), levels.end());
                return levels;
            }

            // "9:300 13:500": false if a step does not read.
            bool ParsePrices(std::string const& text, std::vector<PriceStep>& out)
            {
                std::istringstream in(text);
                std::string token;
                while (in >> token)
                {
                    std::size_t const colon = token.find(':');
                    if (colon == std::string::npos)
                        return false;
                    PriceStep step;
                    step.from = uint32(std::strtoul(token.substr(0, colon).c_str(), nullptr, 10));
                    step.gold = uint32(std::strtoul(token.substr(colon + 1).c_str(), nullptr, 10));
                    if (!step.from)
                        return false;
                    out.push_back(step);
                }
                std::sort(out.begin(), out.end(), [](PriceStep const& a, PriceStep const& b) { return a.from < b.from; });
                return true;
            }
        }

        void LoadConfig()
        {
            g_freeLevels = ParseLevels(sConfig.GetStringDefault("Companion.Slots.FreeLevels", DEFAULT_FREE_LEVELS));
            g_prices.clear();
            std::string const prices = sConfig.GetStringDefault("Companion.Slots.Prices", DEFAULT_PRICES);
            if (!ParsePrices(prices, g_prices))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Companion.Slots.Prices '%s' does not read (\"slot:gold slot:gold\"), using '%s'.",
                    prices.c_str(), DEFAULT_PRICES);
                g_prices.clear();
                ParsePrices(DEFAULT_PRICES, g_prices);
            }
            g_max = std::max<uint32>(sConfig.GetIntDefault("Companion.Slots.Max", DEFAULT_MAX), uint32(g_freeLevels.size()));
        }

        uint32 FreeAt(uint32 level)
        {
            return uint32(std::upper_bound(g_freeLevels.begin(), g_freeLevels.end(), level) - g_freeLevels.begin());
        }

        uint32 NextFreeLevel(uint32 level)
        {
            auto const next = std::upper_bound(g_freeLevels.begin(), g_freeLevels.end(), level);
            return next != g_freeLevels.end() ? *next : 0;
        }

        uint32 Max()
        {
            return g_max;
        }

        uint32 PriceOf(uint32 slot)
        {
            if (slot <= g_freeLevels.size() || slot > g_max)
                return 0;
            // (below the first step: its price)
            uint32 gold = g_prices.empty() ? 0 : g_prices.front().gold;
            for (PriceStep const& step : g_prices)
                if (step.from <= slot)
                    gold = step.gold;
            // A level 60 buys them with coin from dungeons and raids: the price follows the money
            // rate (slots doc §2.2), rounded to whole gold. The experience rate does not matter
            // here: slots are bought at the level cap.
            float const rate = std::max(0.0f, sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
            return uint32(float(gold) * rate + 0.5f) * 10000;   // GOLD
        }
    }
}
