#include "Companions/Affinity/Traits.h"
#include "Companions/PastRoll.h"
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
                { Trait::Diligent,     L, N, "diligent",     "diligent", 0, true,  "勤快",       "仗打得越久，歇下来恢复得越快。" },
                { Trait::NightOwl,     L, N, "nightowl",     "nightowl", 0, true,  "夜猫子",     "夜里休息得更快。" },
                { Trait::EarlyBird,    L, N, "earlybird",    nullptr,    0, true,  "早鸟",       "白天休息得更快。" },
                { Trait::RainLover,    L, N, "rainlover",    nullptr,    0, true,  "爱下雨",     "下雨下雪的时候休息得更快。" },
                { Trait::Appetite,     L, N, "appetite",     nullptr,    0, true,  "好胃口",     "吃饱以后的劲头持续得更久。" },
                { Trait::Frugal,       L, N, "frugal",       "frugal",   0, true,  "节俭",       "修理便宜一点，灰色杂物能多卖几个铜板。" },
                { Trait::Thrifty,      L, N, "thrifty",      "frugal",   0, true,  "会过日子",   "买吃喝、弹药和施法材料便宜一点。" },
                { Trait::Careful,      L, N, "careful",      "frugal",   0, true,  "惜物",       "装备磨损得慢。" },
                { Trait::HardWorker,   L, N, "hardworker",   "diligent", 0, false, "勤工",       "不在你身边时攒的钱多一点。" },
                { Trait::Light,        L, N, "light",        nullptr,    0, true,  "身轻",       "摔下来伤得轻。" },
                { Trait::Swimmer,      L, N, "swimmer",      nullptr,    0, true,  "水性好",     "憋气更久，游得更快。" },
                { Trait::QuickHands,   L, N, "quickhands",   "crafty",   0, true,  "手快",       "采集和制作的动作更快。" },
                { Trait::Crafty,       L, R, "crafty",       "crafty",   0, true,  "手巧",       "做一次能出好几个的东西时，偶尔多做出一个。" },
                { Trait::Saver,        L, R, "saver",        "crafty",   0, true,  "省料",       "做东西时偶尔省下一份最便宜的材料。" },
                { Trait::Gatherer,     L, R, "gatherer",     "gatherer", 0, true,  "爱采集",     "采集时偶尔多采一份。" },
                { Trait::Pickup,       L, R, "pickup",       "gatherer", 0, false, "顺手",       "摸尸体时偶尔多捡到点杂物。" },
                { Trait::Scrounger,    L, R, "scrounger",    "frugal",   0, true,  "捡漏",       "分到的金币多一点。" },
                { Trait::Likeable,     L, R, "likeable",     "chatty",   0, true,  "人缘",       "声望涨得快一点。" },
                { Trait::Pathfinder,   L, R, "pathfinder",   "homebody", 0, true,  "熟路",       "被你叫过来时到得更快。" },
                { Trait::Wanderer,     L, R, "wanderer",     "diligent", 0, true,  "远行",       "不在队里时，远征包裹寄得更勤。" },
                { Trait::Quiet,        L, E, "quiet",        "quiet",    0, true,  "安静",       "野外的怪不容易注意到他。" },
                { Trait::SharpEye,     L, E, "sharpeye",     "gatherer", 0, false, "眼尖",       "采集时更容易找到宝石之类的稀罕东西。" },
                { Trait::Swift,        L, E, "swift",        "homebody", 0, true,  "神行",       "被你叫过来时快得多，过海也不用等船。" },
                { Trait::Veteran,      L, E, "veteran",      "diligent", 0, false, "老练",       "远征包裹里偶尔有一件合用的装备。" },

                { Trait::ThickSkin,    C, N, "thickskin",    nullptr, 0,       false, "皮厚",       "被暴击时伤得轻一点。" },
                { Trait::Constitution, C, N, "constitution", nullptr, 0,       true,  "体质",       "对一系法术的抵抗更强。" },
                { Trait::Revive,       C, N, "revive",       nullptr, 0,       true,  "回神",       "被复活时起身的状态好一些。" },
                { Trait::PoisonResist, C, N, "poisonres",    nullptr, 0,       true,  "耐毒",       "中毒和疾病持续得短一点。" },
                { Trait::Sturdy,       C, N, "sturdy",       nullptr, 0,       true,  "壮实",       "血更厚一点。" },
                { Trait::Steady,       C, N, "steady",       nullptr, CASTERS, true,  "稳手",       "读条被打时推迟得少一点。" },
                { Trait::Brave,        C, N, "brave",        nullptr, 0,       true,  "胆大",       "被恐惧的时间短一点。" },
                { Trait::Hardhead,     C, N, "hardhead",     nullptr, 0,       true,  "硬骨头",     "被击晕的时间短一点。" },
                { Trait::SureFoot,     C, N, "surefoot",     nullptr, 0,       true,  "脚稳",       "被减速和定身的时间短一点。" },
                { Trait::LastStand,    C, N, "laststand",    nullptr, 0,       true,  "舍命",       "快倒下时偶尔能硬撑一下。" },
                { Trait::Breather,     C, N, "breather",     nullptr, 0,       true,  "喘口气",     "刚打完的那几秒恢复得特别快。" },
                { Trait::SelfCare,     C, N, "selfcare",     nullptr, 0,       false, "会照顾自己", "药水和绷带对他更管用。" },
                { Trait::Tough,        C, R, "tough",        nullptr, 0,       true,  "皮实",       "各系法术的抵抗都更强。" },
                { Trait::Composed,     C, R, "composed",     nullptr, 0,       true,  "心定",       "被控制的时间短一点。" },
                { Trait::Desperate,    C, R, "desperate",    nullptr, 0,       true,  "临危",       "血少的时候更扛打。" },
                { Trait::BackToWall,   C, R, "backtowall",   nullptr, 0,       true,  "背水",       "队友倒下后，他会咬牙顶住一阵。" },
                { Trait::Wary,         C, R, "wary",         nullptr, 0,       false, "警惕",       "被几个怪围着时更会躲。" },
                { Trait::Rooted,       C, R, "rooted",       nullptr, 0,       true,  "铁脚",       "站稳以后更扛打。" },
                { Trait::Dodgy,        C, R, "dodgy",        nullptr, 0,       true,  "闪身",       "范围伤害打在他身上轻一点。" },
                { Trait::Alert,        C, R, "alert",        nullptr, 0,       true,  "警觉",       "更容易发现潜行的敌人。" },
                { Trait::Focus,        C, E, "focus",        nullptr, CASTERS, true,  "专注",       "被打断后能更快重新施法。" },
                { Trait::Guard,        C, E, "guard",        nullptr, 0,       true,  "护身",       "挨大招时偶尔能卸掉一部分力。" },
                { Trait::Counter,      C, E, "counter",      nullptr, 0,       false, "后发",       "被暴击后会马上护住自己一阵。" },
                { Trait::Grit,         C, E, "grit",         nullptr, 0,       true,  "余勇",       "血少的时候，治疗对他更管用。" },
            };

            uint32 const RARITY_WEIGHT[] = { 60, 30, 10 };

            bool Fits(Info const& info, Tier tier, uint32 classBit)
            {
                return info.ready && info.tier == tier && (!info.classMask || (info.classMask & classBit));
            }

            char const* SchoolName(uint32 school)
            {
                switch (school)
                {
                    case SPELL_SCHOOL_FIRE:   return "火焰";
                    case SPELL_SCHOOL_NATURE: return "自然";
                    case SPELL_SCHOOL_FROST:  return "冰霜";
                    case SPELL_SCHOOL_SHADOW: return "暗影";
                    default:                  return "奥术";
                }
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

        std::string TextFor(Info const& info, uint32 charGuid)
        {
            if (info.trait == Trait::Constitution)
                return std::string("对") + SchoolName(ConstitutionSchool(charGuid)) + "法术的抵抗更强。";
            return info.text;
        }
    }
}
