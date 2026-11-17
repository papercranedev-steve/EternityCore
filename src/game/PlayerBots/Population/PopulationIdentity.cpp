#include "Population/PopulationIdentity.h"
#include "Database/DatabaseEnv.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "Log.h"
#include "Utilities/Random.h"
#include <cctype>

namespace Population
{
    namespace
    {
        struct SyllableSet
        {
            std::vector<char const*> heads;
            std::vector<char const*> tails;
        };

        SyllableSet const& SyllablesFor(uint8 race)
        {
            static SyllableSet const human = {
                { "Al", "Ber", "Cal", "Dar", "Ed", "Gar", "Hal", "Jor", "Kel", "Mar", "Ner", "Ros", "Ser", "Tal", "Wil", "El", "Ash", "Bran", "Cor", "Lyn", "Rob", "Ga", "Ali", "Mer" },
                { "ric", "win", "den", "ian", "ard", "ton", "vin", "wyn", "anor", "ella", "iss", "ey", "old", "mund", "a", "ana", "ine", "ert", "eth", "ias" } };
            static SyllableSet const dwarf = {
                { "Bael", "Brom", "Dur", "Gim", "Grun", "Kar", "Mag", "Thor", "Bof", "Dag", "Har", "Mur", "Bal", "Thur", "Bru", "Hel", "Kil", "Rag" },
                { "grim", "dain", "in", "gar", "nir", "drum", "li", "ak", "bek", "dan", "ric", "mek", "gen", "tok", "ga", "da" } };
            static SyllableSet const gnome = {
                { "Fiz", "Gim", "Nim", "Tink", "Bink", "Wiz", "Spark", "Cog", "Pip", "Zap", "Fen", "Mek", "Bix", "Dink", "Pim", "Tob" },
                { "wick", "ble", "zle", "bolt", "nob", "gle", "fizz", "kin", "sy", "wiz", "ple", "ket", "nik", "zer" } };
            static SyllableSet const nightElf = {
                { "Ael", "Ela", "Fan", "Ily", "Lor", "Mal", "Sha", "Tyr", "Val", "Syl", "Ara", "Ner", "Fal", "Ith", "Ky", "Ty", "Sel", "Nae" },
                { "andra", "dris", "thas", "wen", "rion", "nae", "dor", "iel", "ara", "yth", "lis", "dre", "ius", "anna", "orn", "elle" } };
            static SyllableSet const orc = {
                { "Grom", "Thra", "Kar", "Mok", "Naz", "Rok", "Dur", "Gor", "Zug", "Brak", "Ogr", "Kro", "Gar", "Gul", "Dra", "Ush", "Mag", "Rag" },
                { "gar", "nak", "gash", "thar", "mosh", "rok", "ug", "dan", "gul", "osh", "grim", "tok", "nar", "rash", "zug", "kar" } };
            static SyllableSet const troll = {
                { "Zul", "Jin", "Vol", "Raz", "Sen", "Tak", "Zan", "Mal", "Ka", "Jen", "Rok", "Zal", "Tal", "Vash", "Mua" },
                { "jin", "zek", "kai", "ja", "tuk", "rak", "zal", "zi", "shi", "dazar", "jan", "ko", "lo", "ga", "tu" } };
            static SyllableSet const tauren = {
                { "Bai", "Hu", "Mu", "Tah", "Ka", "Ha", "Gro", "Tor", "Ma", "Ruk", "Ah", "Mo", "Tal", "Kodo", "Una" },
                { "rnes", "hoof", "sha", "rnak", "wa", "ga", "lo", "ruk", "tahn", "hn", "mor", "ka", "dan", "nok", "wen" } };
            static SyllableSet const undead = {
                { "Mor", "Vel", "Gra", "Sar", "Ner", "Cal", "Mal", "Dra", "Vex", "Kel", "Bel", "Sil", "Zer", "Gal", "Rot" },
                { "gath", "vin", "ius", "dred", "thos", "ren", "ix", "eth", "vane", "ric", "ath", "ok", "us", "ane", "is" } };

            switch (race)
            {
                case RACE_DWARF:    return dwarf;
                case RACE_GNOME:    return gnome;
                case RACE_NIGHTELF: return nightElf;
                case RACE_ORC:      return orc;
                case RACE_TROLL:    return troll;
                case RACE_TAUREN:   return tauren;
                case RACE_UNDEAD:   return undead;
                default:            return human;
            }
        }

        // Names like the ones real players pick ("Shadowfang", "Ironheart").
        char const* const g_styleHeads[] = { "Shadow", "Dark", "Storm", "Frost", "Blood", "Moon", "Sun", "Iron", "Night", "Grim", "Wolf", "Fire", "Stone", "Thunder", "Ash", "Raven", "Silver", "Wild" };
        char const* const g_styleTails[] = { "blade", "fang", "heart", "strike", "walker", "bane", "shade", "hunter", "fury", "mane", "born", "claw", "fist", "song", "hide", "brew" };

        bool IsLettersOnly(std::string const& name)
        {
            for (char c : name)
                if (!std::isalpha(static_cast<unsigned char>(c)))
                    return false;
            return true;
        }

        bool IsUsable(std::string const& name, std::set<std::string> const& reserved)
        {
            return name.size() >= 2 && name.size() <= MAX_PLAYER_NAME && IsLettersOnly(name) &&
                   !reserved.count(name) && sObjectMgr.GetPlayerGuidByName(name).IsEmpty();
        }

        std::vector<uint8> const& TeamRaces(uint32 team)
        {
            static std::vector<uint8> const alliance = { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME };
            static std::vector<uint8> const horde = { RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL };
            return team == HORDE ? horde : alliance;
        }
    }

    void Identity::Load()
    {
        m_anyRace.clear();
        m_byRace.assign(MAX_RACES, {});

        std::unique_ptr<QueryResult> exists(WorldDatabase.Query("SHOW TABLES LIKE 'custom_popbot_name'"));
        if (!exists)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Table custom_popbot_name not found (sql/custom/popbot.sql), using built-in names only.");
            return;
        }

        std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `name`, `race` FROM `custom_popbot_name`"));
        if (!result)
            return;

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            std::string name = fields[0].GetCppString();
            uint32 const race = fields[1].GetUInt32();
            if (!normalizePlayerName(name, MAX_PLAYER_NAME) || name.size() < 2 || !IsLettersOnly(name))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] custom_popbot_name: skipping invalid name '%s'.", fields[0].GetString());
                continue;
            }
            if (race == 0)
                m_anyRace.push_back(name);
            else if (race < MAX_RACES)
                m_byRace[race].push_back(name);
            else
                continue;
            ++count;
        } while (result->NextRow());

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Loaded %u names from custom_popbot_name.", count);
    }

    uint32 Identity::GetTableNameCount() const
    {
        size_t count = m_anyRace.size();
        for (auto const& names : m_byRace)
            count += names.size();
        return uint32(count);
    }

    uint32 Identity::Roll(std::mt19937* rng, uint32 lo, uint32 hi)
    {
        if (hi <= lo)
            return lo;
        if (!rng)
            return urand(lo, hi);
        return std::uniform_int_distribution<uint32>(lo, hi)(*rng);
    }

    std::string Identity::RandomBuiltInName(uint8 race, std::mt19937* rng) const
    {
        std::string name;
        if (Roll(rng, 0, 99) < 25)
        {
            name = g_styleHeads[Roll(rng, 0, uint32(sizeof(g_styleHeads) / sizeof(g_styleHeads[0])) - 1)];
            name += g_styleTails[Roll(rng, 0, uint32(sizeof(g_styleTails) / sizeof(g_styleTails[0])) - 1)];
        }
        else
        {
            SyllableSet const& set = SyllablesFor(race);
            name = set.heads[Roll(rng, 0, uint32(set.heads.size()) - 1)];
            name += set.tails[Roll(rng, 0, uint32(set.tails.size()) - 1)];
        }
        normalizePlayerName(name, MAX_INTERNAL_PLAYER_NAME);
        return name;
    }

    std::string Identity::MakeName(uint8 race, std::set<std::string> const& reserved, std::mt19937* rng) const
    {
        // Table names first (half of the time when the table has names for this race).
        std::vector<std::string const*> table;
        for (std::string const& n : m_anyRace)
            table.push_back(&n);
        if (race < m_byRace.size())
            for (std::string const& n : m_byRace[race])
                table.push_back(&n);

        if (!table.empty() && Roll(rng, 0, 1))
        {
            for (uint32 attempt = 0; attempt < 10; ++attempt)
            {
                std::string const& name = *table[Roll(rng, 0, uint32(table.size()) - 1)];
                if (IsUsable(name, reserved))
                    return name;
            }
        }

        for (uint32 attempt = 0; attempt < 40; ++attempt)
        {
            std::string name = RandomBuiltInName(race, rng);
            if (IsUsable(name, reserved))
                return name;
        }
        return std::string();
    }

    bool Identity::IsValidCombo(uint8 race, uint8 classId)
    {
        return sObjectMgr.GetPlayerInfo(race, classId) != nullptr;
    }

    uint8 Identity::RandomRace(uint32 team, std::mt19937* rng)
    {
        std::vector<uint8> const& races = TeamRaces(team);
        return races[Roll(rng, 0, uint32(races.size()) - 1)];
    }

    uint8 Identity::RandomClass(uint8 race, std::mt19937* rng)
    {
        std::vector<uint8> classes;
        for (uint8 c = CLASS_WARRIOR; c < MAX_CLASSES; ++c)
            if (IsValidCombo(race, c))
                classes.push_back(c);
        if (classes.empty())
            return 0;
        return classes[Roll(rng, 0, uint32(classes.size()) - 1)];
    }


    uint8 Identity::RandomCityLevel()
    {
        if (urand(0, 1))
            return 60;
        return uint8(urand(10, 59));
    }
}
