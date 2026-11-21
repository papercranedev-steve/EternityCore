#include "Companions/Affinity/Traits.h"
#include "Companions/PastRoll.h"
#include "Shared/Phrases.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Companions
{
    namespace Traits
    {
        namespace
        {
            // Class mask of casters (稳手, 专注: pushback and lockouts only hurt them).
            uint32 constexpr CASTERS = (1 << (CLASS_PALADIN - 1)) | (1 << (CLASS_PRIEST - 1)) | (1 << (CLASS_SHAMAN - 1)) |
                                       (1 << (CLASS_MAGE - 1)) | (1 << (CLASS_WARLOCK - 1)) | (1 << (CLASS_DRUID - 1));

            // PastRoll keys: class quest chains use quest ids, professions the high bit; traits 0x4000xxxx.
            uint32 constexpr KEY_RARITY = 0x40000000;
            uint32 constexpr KEY_PICK = 0x40000010;
            uint32 constexpr KEY_SCHOOL = 0x40000020;

            Tier constexpr L = Tier::Life;
            Tier constexpr C = Tier::Combat;
            Rarity constexpr N = Rarity::Common;
            Rarity constexpr R = Rarity::Rare;
            Rarity constexpr E = Rarity::Epic;

            // Design doc §4 (life) and §5 (combat). Not ready: 勤工 (no offline earnings yet),
            // 顺手 / 眼尖 (per-looter loot rolls), 老练 (needs bench surprise gifts, B4),
            // 皮厚 / 后发 (the damage hook does not know crits), 警惕 (dodge), 会照顾自己 (potions, bandages).
            std::vector<Info> const g_traits = {
                { Trait::Diligent,     L, N, "diligent",     "diligent", 0, true },
                { Trait::NightOwl,     L, N, "nightowl",     "nightowl", 0, true },
                { Trait::EarlyBird,    L, N, "earlybird",    nullptr,    0, true },
                { Trait::RainLover,    L, N, "rainlover",    nullptr,    0, true },
                { Trait::Appetite,     L, N, "appetite",     nullptr,    0, true },
                { Trait::Frugal,       L, N, "frugal",       "frugal",   0, true },
                { Trait::Thrifty,      L, N, "thrifty",      "frugal",   0, true },
                { Trait::Careful,      L, N, "careful",      "frugal",   0, true },
                { Trait::HardWorker,   L, N, "hardworker",   "diligent", 0, false },
                { Trait::Light,        L, N, "light",        nullptr,    0, true },
                { Trait::Swimmer,      L, N, "swimmer",      nullptr,    0, true },
                { Trait::QuickHands,   L, N, "quickhands",   "crafty",   0, true },
                { Trait::Crafty,       L, R, "crafty",       "crafty",   0, true },
                { Trait::Saver,        L, R, "saver",        "crafty",   0, true },
                { Trait::Gatherer,     L, R, "gatherer",     "gatherer", 0, true },
                { Trait::Pickup,       L, R, "pickup",       "gatherer", 0, false },
                { Trait::Scrounger,    L, R, "scrounger",    "frugal",   0, true },
                { Trait::Likeable,     L, R, "likeable",     "chatty",   0, true },
                { Trait::Pathfinder,   L, R, "pathfinder",   "homebody", 0, true },
                { Trait::Wanderer,     L, R, "wanderer",     "diligent", 0, true },
                { Trait::Quiet,        L, E, "quiet",        "quiet",    0, true },
                { Trait::SharpEye,     L, E, "sharpeye",     "gatherer", 0, false },
                { Trait::Swift,        L, E, "swift",        "homebody", 0, true },
                { Trait::Veteran,      L, E, "veteran",      "diligent", 0, false },

                { Trait::ThickSkin,    C, N, "thickskin",    nullptr, 0,       false },
                { Trait::Constitution, C, N, "constitution", nullptr, 0,       true },
                { Trait::Revive,       C, N, "revive",       nullptr, 0,       true },
                { Trait::PoisonResist, C, N, "poisonres",    nullptr, 0,       true },
                { Trait::Sturdy,       C, N, "sturdy",       nullptr, 0,       true },
                { Trait::Steady,       C, N, "steady",       nullptr, CASTERS, true },
                { Trait::Brave,        C, N, "brave",        nullptr, 0,       true },
                { Trait::Hardhead,     C, N, "hardhead",     nullptr, 0,       true },
                { Trait::SureFoot,     C, N, "surefoot",     nullptr, 0,       true },
                { Trait::LastStand,    C, N, "laststand",    nullptr, 0,       true },
                { Trait::Breather,     C, N, "breather",     nullptr, 0,       true },
                { Trait::SelfCare,     C, N, "selfcare",     nullptr, 0,       false },
                { Trait::Tough,        C, R, "tough",        nullptr, 0,       true },
                { Trait::Composed,     C, R, "composed",     nullptr, 0,       true },
                { Trait::Desperate,    C, R, "desperate",    nullptr, 0,       true },
                { Trait::BackToWall,   C, R, "backtowall",   nullptr, 0,       true },
                { Trait::Wary,         C, R, "wary",         nullptr, 0,       false },
                { Trait::Rooted,       C, R, "rooted",       nullptr, 0,       true },
                { Trait::Dodgy,        C, R, "dodgy",        nullptr, 0,       true },
                { Trait::Alert,        C, R, "alert",        nullptr, 0,       true },
                { Trait::Focus,        C, E, "focus",        nullptr, CASTERS, true },
                { Trait::Guard,        C, E, "guard",        nullptr, 0,       true },
                { Trait::Counter,      C, E, "counter",      nullptr, 0,       false },
                { Trait::Grit,         C, E, "grit",         nullptr, 0,       true },
            };

            uint32 const RARITY_WEIGHT[] = { 60, 30, 10 };

            bool Fits(Info const& info, Tier tier, uint32 classBit)
            {
                return info.ready && info.tier == tier && (!info.classMask || (info.classMask & classBit));
            }


        }

        std::vector<Info> const& All()
        {
            return g_traits;
        }

        Info const* Of(Trait trait)
        {
            for (Info const& info : g_traits)
                if (info.trait == trait)
                    return &info;
            return nullptr;
        }

        Info const* Find(std::string const& key)
        {
            if (key.empty())
                return nullptr;
            for (Info const& info : g_traits)
                if (key == info.key)
                    return &info;
            return nullptr;
        }

        uint32 TierAt(Tier tier)
        {
            return tier == Tier::Life ? LIFE_AT : COMBAT_AT;
        }

        Trait Roll(uint32 charGuid, Tier tier, uint8 classId, std::vector<std::string> const& tags)
        {
            uint32 const classBit = classId ? 1u << (classId - 1) : 0;
            uint32 const t = uint32(tier);

            // The rarity: among those with a trait for him.
            uint32 rarityWeights[3] = { 0, 0, 0 };
            for (Info const& info : g_traits)
                if (Fits(info, tier, classBit))
                    rarityWeights[uint32(info.rarity)] = RARITY_WEIGHT[uint32(info.rarity)];
            uint32 const rarityTotal = rarityWeights[0] + rarityWeights[1] + rarityWeights[2];
            if (!rarityTotal)
                return Trait::None;
            float pick = PastRoll(charGuid, KEY_RARITY + t) * rarityTotal;
            uint32 rarity = 0;
            for (; rarity < 2; ++rarity)
            {
                if (pick < rarityWeights[rarity])
                    break;
                pick -= rarityWeights[rarity];
            }
            while (!rarityWeights[rarity])      // (float edge: the last one with a weight)
                --rarity;

            // A trait of that rarity: 3x for one of his personality tags.
            std::vector<std::pair<Info const*, uint32>> candidates;
            uint32 total = 0;
            for (Info const& info : g_traits)
            {
                if (!Fits(info, tier, classBit) || uint32(info.rarity) != rarity)
                    continue;
                bool const liked = info.tag && std::find(tags.begin(), tags.end(), info.tag) != tags.end();
                uint32 const weight = liked ? 3 : 1;
                candidates.emplace_back(&info, weight);
                total += weight;
            }
            float at = PastRoll(charGuid, KEY_PICK + t) * total;
            for (auto const& c : candidates)
            {
                if (at < c.second)
                    return c.first->trait;
                at -= c.second;
            }
            return candidates.back().first->trait;
        }

        uint32 ConstitutionSchool(uint32 charGuid)
        {
            uint32 const schools[] = { SPELL_SCHOOL_FIRE, SPELL_SCHOOL_NATURE, SPELL_SCHOOL_FROST, SPELL_SCHOOL_SHADOW, SPELL_SCHOOL_ARCANE };
            uint32 const i = std::min<uint32>(4, uint32(PastRoll(charGuid, KEY_SCHOOL) * 5));
            return schools[i];
        }

        std::string Label(Info const& info, BotShared::I18n::Lang lang)
        {
            return BotShared::Phrases::Word(lang, std::string("trait.") + info.key);
        }

        std::string TextFor(Info const& info, uint32 charGuid, BotShared::I18n::Lang lang)
        {
            std::string const key = std::string("trait.") + info.key + ".text";
            if (info.trait == Trait::Constitution)
                return BotShared::Phrases::Text(lang, key,
                    { { "school", BotShared::Phrases::Word(lang, "school." + std::to_string(ConstitutionSchool(charGuid))) } });
            return BotShared::Phrases::Word(lang, key);
        }
    }
}
