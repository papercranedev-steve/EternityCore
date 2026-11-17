#include "Companions/Personality.h"
#include <algorithm>

namespace Companions
{
    namespace
    {
        // Exclusive groups: 1 = how hard they work, 2 = how they treat money, 3 = how much they talk.
        std::vector<TagDef> const g_tags = {
            // Positive
            { "diligent",    TagPolarity::Positive, 1 },    // offline: earns a little money
            { "crafty",      TagPolarity::Positive, 0 },    // offline: profession skill
            { "frugal",      TagPolarity::Positive, 2 },    // offline: money never goes down
            { "gatherer",    TagPolarity::Positive, 0 },    // offline: herbs, ore, skins
            // Neutral
            { "homebody",    TagPolarity::Neutral,  0 },    // stays in the city
            { "chatty",      TagPolarity::Neutral,  3 },
            { "quiet",       TagPolarity::Neutral,  3 },
            { "nightowl",    TagPolarity::Neutral,  0 },
            // Negative
            { "spendthrift", TagPolarity::Negative, 2 },    // offline: spends part of the money
            { "careless",    TagPolarity::Negative, 0 },    // offline: gear loses durability
            { "lazy",        TagPolarity::Negative, 1 },    // offline: does nothing
        };

        // How many tags of each kind a person gets.
        uint32 const POSITIVE_MIN = 1, POSITIVE_MAX = 2;
        uint32 const NEUTRAL_MIN = 1, NEUTRAL_MAX = 2;
        uint32 const NEGATIVE_PCT = 40;     // chance of one negative tag

        uint32 RollRange(std::mt19937& rng, uint32 min, uint32 max)
        {
            return std::uniform_int_distribution<uint32>(min, max)(rng);
        }

        // Adds up to `count` tags of this polarity that do not clash with the chosen ones.
        void Pick(std::mt19937& rng, TagPolarity polarity, uint32 count, std::vector<TagDef const*>& chosen)
        {
            for (uint32 i = 0; i < count; ++i)
            {
                std::vector<TagDef const*> options;
                for (TagDef const& tag : g_tags)
                {
                    if (tag.polarity != polarity)
                        continue;
                    bool const clash = std::any_of(chosen.begin(), chosen.end(), [&tag](TagDef const* c)
                    {
                        return c == &tag || (tag.exclusiveGroup && c->exclusiveGroup == tag.exclusiveGroup);
                    });
                    if (!clash)
                        options.push_back(&tag);
                }
                if (options.empty())
                    return;
                chosen.push_back(options[RollRange(rng, 0, uint32(options.size()) - 1)]);
            }
        }
    }

    namespace Personality
    {
        std::vector<TagDef> const& AllTags()
        {
            return g_tags;
        }

        TagDef const* FindTag(std::string const& key)
        {
            for (TagDef const& tag : g_tags)
                if (key == tag.key)
                    return &tag;
            return nullptr;
        }

        std::vector<std::string> Roll(std::mt19937& rng)
        {
            std::vector<TagDef const*> chosen;
            // Negative first: the positive picks then avoid its exclusive group (no frugal spendthrift).
            // At most one negative, and at least one positive, so negatives never outnumber positives.
            if (RollRange(rng, 0, 99) < NEGATIVE_PCT)
                Pick(rng, TagPolarity::Negative, 1, chosen);
            Pick(rng, TagPolarity::Positive, RollRange(rng, POSITIVE_MIN, POSITIVE_MAX), chosen);
            Pick(rng, TagPolarity::Neutral, RollRange(rng, NEUTRAL_MIN, NEUTRAL_MAX), chosen);

            std::vector<std::string> keys;
            keys.reserve(chosen.size());
            for (TagDef const* tag : chosen)
                keys.emplace_back(tag->key);
            return keys;
        }
    }
}
