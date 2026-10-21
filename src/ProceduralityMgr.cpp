/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 * Formerly core patch 0004-procgen (wowproject).
 */

#include "ProceduralityMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Containers.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ForgeMgr.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GridDefines.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "RaceMgr.h"
#include "Random.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>

namespace
{
    constexpr uint32 CONTINENTS[] = { 0, 1, 530, 571 };
    constexpr float CELL_SIZE = 64.0f;
    constexpr float MAP_EDGE = 32.0f * SIZE_OF_GRIDS;
    constexpr uint32 MAX_GIVER_QUESTS = 20;  // GossipDef asserts at 32 menu items
    constexpr float MARKER_REFRESH_YARDS = 150.0f;
    constexpr float KILL_CREDIT_MARGIN = 30.0f;   // past a bounty's radius: wander (20 yd) plus a short chase

    // Same column order as ObjectMgr::LoadQuests, so Quest(Field*) reads it unchanged.
    constexpr char const* QUEST_COLUMNS =
        "ID, QuestType, QuestLevel, MinLevel, QuestSortID, QuestInfoID, SuggestedGroupNum, TimeAllowed, AllowableRaces, "
        "RequiredFactionId1, RequiredFactionId2, RequiredFactionValue1, RequiredFactionValue2, "
        "RewardNextQuest, RewardXPDifficulty, RewardMoney, RewardMoneyDifficulty, RewardDisplaySpell, RewardSpell, RewardHonor, RewardKillHonor, "
        "StartItem, Flags, RewardTitle, RequiredPlayerKills, RewardTalents, RewardArenaPoints, "
        "RewardItem1, RewardAmount1, RewardItem2, RewardAmount2, RewardItem3, RewardAmount3, RewardItem4, RewardAmount4, "
        "RewardChoiceItemID1, RewardChoiceItemQuantity1, RewardChoiceItemID2, RewardChoiceItemQuantity2, RewardChoiceItemID3, RewardChoiceItemQuantity3, "
        "RewardChoiceItemID4, RewardChoiceItemQuantity4, RewardChoiceItemID5, RewardChoiceItemQuantity5, RewardChoiceItemID6, RewardChoiceItemQuantity6, "
        "RewardFactionID1, RewardFactionValue1, RewardFactionOverride1, RewardFactionID2, RewardFactionValue2, RewardFactionOverride2, "
        "RewardFactionID3, RewardFactionValue3, RewardFactionOverride3, RewardFactionID4, RewardFactionValue4, RewardFactionOverride4, "
        "RewardFactionID5, RewardFactionValue5, RewardFactionOverride5, "
        "POIContinent, POIx, POIy, POIPriority, "
        "LogTitle, LogDescription, QuestDescription, AreaDescription, QuestCompletionLog, "
        "RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4, RequiredNpcOrGoCount1, RequiredNpcOrGoCount2, RequiredNpcOrGoCount3, RequiredNpcOrGoCount4, "
        "ItemDrop1, ItemDrop2, ItemDrop3, ItemDrop4, ItemDropQuantity1, ItemDropQuantity2, ItemDropQuantity3, ItemDropQuantity4, "
        "RequiredItemId1, RequiredItemId2, RequiredItemId3, RequiredItemId4, RequiredItemId5, RequiredItemId6, "
        "RequiredItemCount1, RequiredItemCount2, RequiredItemCount3, RequiredItemCount4, RequiredItemCount5, RequiredItemCount6, "
        "Unknown0, ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4";

    uint32 CellKey(float x, float y)
    {
        uint32 const cx = uint32(std::clamp((x + MAP_EDGE) / CELL_SIZE, 0.0f, 1023.0f));
        uint32 const cy = uint32(std::clamp((y + MAP_EDGE) / CELL_SIZE, 0.0f, 1023.0f));
        return (cx << 10) | cy;
    }

    float Dist2(float ax, float ay, float bx, float by)
    {
        return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
    }

    bool IsRealPlayer(Player const* player)
    {
        return player && player->GetSession() && !player->GetSession()->IsSocketClosed();
    }

    // English plural of a creature name: "Defias Thug" -> "Defias Thugs", "Young Wolf" -> "Young Wolves",
    // "Servant of Azora" -> "Servants of Azora".
    std::string Plural(std::string const& name)
    {
        std::size_t const of = name.find(" of ");
        if (of != std::string::npos)
            return Plural(name.substr(0, of)) + name.substr(of);

        std::size_t const space = name.rfind(' ');
        std::string const head = space == std::string::npos ? "" : name.substr(0, space + 1);
        std::string word = space == std::string::npos ? name : name.substr(space + 1);
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        static std::unordered_map<std::string, std::string> const irregular = {
            { "wolf", "wolves" }, { "thief", "thieves" }, { "man", "men" }, { "woman", "women" },
            { "dwarf", "dwarves" }, { "elf", "elves" }, { "wolfkin", "wolfkin" }, { "vermin", "vermin" },
            { "deer", "deer" }, { "sheep", "sheep" }, { "fish", "fish" }, { "kin", "kin" },
            { "mouse", "mice" }, { "louse", "lice" }, { "ox", "oxen" }, { "child", "children" },
            { "foot", "feet" }, { "tooth", "teeth" }, { "goose", "geese" }, { "worgen", "worgen" },
            { "tauren", "tauren" }, { "furbolg", "furbolgs" }, { "harpy", "harpies" }, { "staff", "staves" },
            { "leaf", "leaves" }, { "wife", "wives" }, { "knife", "knives" }, { "self", "selves" },
        };

        std::string plural;
        if (auto itr = irregular.find(lower); itr != irregular.end())
        {
            plural = itr->second;
            if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0])))
                plural[0] = std::toupper(static_cast<unsigned char>(plural[0]));
        }
        else if (lower.size() > 1 && lower.back() == 'y' && !std::strchr("aeiou", lower[lower.size() - 2]))
            plural = word.substr(0, word.size() - 1) + "ies";
        else if (lower.ends_with("s") || lower.ends_with("x") || lower.ends_with("z") || lower.ends_with("ch") || lower.ends_with("sh"))
            plural = word + "es";
        else if (lower.ends_with("man"))
            plural = word.substr(0, word.size() - 3) + "men";
        else
            plural = word + "s";

        return head + plural;
    }

    std::string Substitute(std::string text, std::unordered_map<std::string, std::string> const& vars)
    {
        // Longest names first, so $targets wins over $target
        std::vector<std::pair<std::string, std::string>> sorted(vars.begin(), vars.end());
        std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.first.size() > b.first.size(); });

        for (auto const& [name, value] : sorted)
        {
            std::string const token = "$" + name;
            for (std::size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
                text.replace(pos, token.size(), value);
        }
        return text;
    }

    // "Northshire Vineyards" reads as "near the Northshire Vineyards"; "Elwynn Forest" and "Goldshire" take none
    std::string WithArticle(std::string const& name)
    {
        if (name.empty() || name.starts_with("The ") || name.starts_with("the "))
            return name;

        static char const* const prefixes[] = { "Ruins of ", "Valley of ", "Vale of ", "Temple of ", "Den of " };
        for (char const* prefix : prefixes)
            if (name.starts_with(prefix))
                return "the " + name;

        static char const* const suffixes[] = { " Vineyards", " Vineyard", " Mine", " Mines", " Farm", " Farmstead", " Woods",
            " Hills", " Highlands", " Marsh", " Fields", " Grove", " Hollow", " Barrows", " Crossroads", " Caverns",
            " Cave", " Quarry", " Lumber Mill", " Mill", " Orchard", " Tower", " Watchtower", " Camp", " Encampment",
            " Outpost", " Pass", " Gorge", " Canyon", " Wastes", " Plains", " Flats", " Shore", " Coast", " Glade" };
        for (char const* suffix : suffixes)
            if (name.ends_with(suffix))
                return "the " + name;

        return name;
    }

    char const* AreaName(uint32 areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        return area ? area->area_name[0] : "";
    }
}

ProceduralityMgr* ProceduralityMgr::instance()
{
    static ProceduralityMgr instance;
    return &instance;
}

void ProceduralityMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("Procedurality.Enable", false);
    _debug = uint8(std::min<uint32>(2, sConfigMgr->GetOption<uint32>("Procedurality.Debug", 0)));
    _intervalMs = std::max<uint32>(10, sConfigMgr->GetOption<uint32>("Procedurality.Interval", 60)) * IN_MILLISECONDS;
    _halfLifeSeconds = std::max(1.0f, sConfigMgr->GetOption<float>("Procedurality.Activity.HalfLifeMinutes", 30.0f)) * MINUTE;
    _maxActive = sConfigMgr->GetOption<uint32>("Procedurality.Bounty.MaxActive", 30);
    _maxPerZone = sConfigMgr->GetOption<uint32>("Procedurality.Bounty.MaxPerZone", 2);
    _maxPerGiver = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Procedurality.Bounty.MaxPerGiver", 3));
    _playerCooldown = sConfigMgr->GetOption<uint32>("Procedurality.Bounty.PlayerCooldownMinutes", 20) * MINUTE;
    _nearbyYards = sConfigMgr->GetOption<float>("Procedurality.Bounty.NearbyYards", 700.0f);
    _searchYards = std::clamp(sConfigMgr->GetOption<float>("Procedurality.Bounty.SearchYards", 700.0f), 100.0f, 1500.0f);
    _minYards = sConfigMgr->GetOption<float>("Procedurality.Bounty.MinYards", 100.0f);
    _campYards = std::clamp(sConfigMgr->GetOption<float>("Procedurality.Bounty.CampYards", 80.0f), 20.0f, 200.0f);
    _minCampSize = std::max<uint32>(2, sConfigMgr->GetOption<uint32>("Procedurality.Bounty.MinCampSize", 5));
    _giverYards = std::clamp(sConfigMgr->GetOption<float>("Procedurality.Bounty.GiverYards", 900.0f), 100.0f, 2000.0f);
    _xpDifficulty = std::min<uint32>(8, sConfigMgr->GetOption<uint32>("Procedurality.Bounty.XPDifficulty", 5));
    _moneyRate = std::max(0.0f, sConfigMgr->GetOption<float>("Procedurality.Bounty.MoneyRate", 1.0f));
    _offerSeconds = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Procedurality.Bounty.OfferMinutes", 120)) * MINUTE;
    _graceSeconds = sConfigMgr->GetOption<uint32>("Procedurality.Bounty.GraceMinutes", 120) * MINUTE;
    _wantedChance = std::clamp(sConfigMgr->GetOption<float>("Procedurality.Wanted.Chance", 35.0f), 0.0f, 100.0f);
    _wantedMaxActive = sConfigMgr->GetOption<uint32>("Procedurality.Wanted.MaxActive", 5);
    _wantedMaxPerZone = sConfigMgr->GetOption<uint32>("Procedurality.Wanted.MaxPerZone", 1);
    _rareHealth = std::max(0.1f, sConfigMgr->GetOption<float>("Procedurality.Wanted.HealthMultiplier", 3.0f));
    _rareDamage = std::max(0.1f, sConfigMgr->GetOption<float>("Procedurality.Wanted.DamageMultiplier", 1.3f));
    _rareRespawnSeconds = sConfigMgr->GetOption<uint32>("Procedurality.Wanted.RespawnMinutes", 10) * MINUTE;
    _rareBlueChance = std::clamp(sConfigMgr->GetOption<float>("Procedurality.Wanted.BlueChance", 25.0f), 0.0f, 100.0f);
    _wantedXpDifficulty = std::min<uint32>(8, sConfigMgr->GetOption<uint32>("Procedurality.Wanted.XPDifficulty", 6));
    _affixRewards = sConfigMgr->GetOption<bool>("Procedurality.Rewards.Affixes", true) && sForgeMgr->IsEnabled();
}

void ProceduralityMgr::Load()
{
    uint32 const oldMSTime = getMSTime();

    LoadConfig();
    for (uint32 mapId : CONTINENTS)
        _activity[mapId];

    // Without its tables (sql/procedurality/) the quest ID counter would start over and reuse IDs clients cache
    if (!CharacterDatabase.Query("SHOW TABLES LIKE 'procedurality_bounty'") || !WorldDatabase.Query("SHOW TABLES LIKE 'procedurality_text'"))
    {
        LOG_ERROR("server.loading", ">> Procedurality: tables missing (apply sql/procedurality/); nothing posted or loaded");
        _enabled = false;
        return;
    }

    LoadTexts();
    LoadWorldMapAreas();
    sObjectMgr->ReserveQuestTemplates(LAST_QUEST_ID);
    LoadRareBases();
    LoadBounties();

    LOG_INFO("server.loading", ">> Procedurality: {} live incidents, {} texts, next quest {}, next rare {} (director {}) in {} ms",
        _bounties.size(), std::accumulate(_texts.begin(), _texts.end(), std::size_t(0), [](std::size_t n, auto const& t) { return n + t.second.size(); }),
        _nextQuestId, _nextRareEntry, _enabled ? "on" : "off", GetMSTimeDiffToNow(oldMSTime));
    _loaded = true;
}

void ProceduralityMgr::LoadTexts()
{
    _texts.clear();
    QueryResult result = WorldDatabase.Query("SELECT kind, type_mask, weight, text FROM procedurality_text");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        _texts[fields[0].Get<std::string>()].push_back({ fields[1].Get<uint32>(), fields[2].Get<uint32>(), fields[3].Get<std::string>() });
    } while (result->NextRow());
}

// The quest POI needs the WorldMapArea.dbc ID of the zone, which the core's store doesn't keep (it indexes
// the file by zone), so read the file directly: header, then records of uint32 ID, map, zone, ...
void ProceduralityMgr::LoadWorldMapAreas()
{
    std::string path = sWorld->GetDataPath();
    if (!path.empty() && path.back() != '/')
        path += '/';
    path += "dbc/WorldMapArea.dbc";

    std::ifstream file(path, std::ios::binary);
    uint32 header[5] = {};
    if (!file || !file.read(reinterpret_cast<char*>(header), sizeof(header)) || header[0] != 0x43424457 /*WDBC*/ || header[3] < 12)
    {
        LOG_ERROR("server.loading", "Procedurality: can't read {}; generated quests get no world map POIs", path);
        return;
    }

    std::vector<char> record(header[3]);
    for (uint32 i = 0; i < header[1] && file.read(record.data(), record.size()); ++i)
    {
        uint32 fields[3];
        std::memcpy(fields, record.data(), sizeof(fields));
        if (fields[2])
            _worldMapAreaByZone.emplace(fields[2], fields[0]);
    }
}

void ProceduralityMgr::LoadBounties()
{
    QueryResult result = CharacterDatabase.Query("SELECT quest_id, map, zone, area, x, y, z, radius, target_entry, kill_count, level, "
        "giver_spawn, giver_entry, for_player, created, offer_until, retire_at, retired, kind, rare_entry, rare_name, rare_level FROM procedurality_bounty");
    if (!result)
        return;

    time_t const now = GameTime::GetGameTime().count();
    do
    {
        Field* fields = result->Fetch();
        ProceduralityBounty bounty;
        bounty.QuestId = fields[0].Get<uint32>();
        bounty.MapId = fields[1].Get<uint32>();
        bounty.ZoneId = fields[2].Get<uint32>();
        bounty.AreaId = fields[3].Get<uint32>();
        bounty.X = fields[4].Get<float>();
        bounty.Y = fields[5].Get<float>();
        bounty.Z = fields[6].Get<float>();
        bounty.Radius = fields[7].Get<float>();
        bounty.TargetEntry = fields[8].Get<uint32>();
        bounty.KillCount = fields[9].Get<uint32>();
        bounty.Level = fields[10].Get<uint8>();
        bounty.GiverSpawnId = fields[11].Get<uint32>();
        bounty.GiverEntry = fields[12].Get<uint32>();
        bounty.ForPlayer = fields[13].Get<uint32>();
        bounty.Created = fields[14].Get<uint32>();
        bounty.OfferUntil = fields[15].Get<uint32>();
        bounty.RetireAt = fields[16].Get<uint32>();
        bool const retired = fields[17].Get<bool>();
        bounty.Kind = fields[18].Get<uint8>();
        bounty.RareEntry = fields[19].Get<uint32>();
        bounty.RareName = fields[20].Get<std::string>();
        bounty.RareLevel = fields[21].Get<uint8>();

        _nextQuestId = std::max(_nextQuestId, bounty.QuestId + 1);
        if (bounty.RareEntry)
            _nextRareEntry = std::max(_nextRareEntry, bounty.RareEntry + 1);
        if (retired || !IsGeneratedQuest(bounty.QuestId))
            continue;

        // A Wanted rare's template is rebuilt from its base before the quest that names it
        bool const rareOk = bounty.Kind != PROCEDURALITY_WANTED
            || FillRareTemplate(bounty.RareEntry, bounty.TargetEntry, bounty.RareName, bounty.RareLevel);
        Quest* quest = now < bounty.RetireAt && rareOk ? LoadQuest(bounty.QuestId) : nullptr;
        if (!quest)
        {
            // Expired while the server was down (or its quest rows are gone): no players are online yet
            RetireRows(bounty.QuestId);
            continue;
        }

        Register(bounty, quest);
        SetOffered(_bounties[bounty.QuestId], now < bounty.OfferUntil);
    } while (result->NextRow());
}

Quest* ProceduralityMgr::LoadQuest(uint32 questId) const
{
    QueryResult result = CharacterDatabase.Query("SELECT {} FROM procedurality_quest_template WHERE ID = {}", QUEST_COLUMNS, questId);
    if (!result)
        return nullptr;

    Quest* quest = new Quest(result->Fetch());

    if (QueryResult texts = CharacterDatabase.Query("SELECT ID, EmoteOnComplete, EmoteOnIncomplete, CompletionText "
        "FROM procedurality_quest_request_items WHERE ID = {}", questId))
        quest->LoadQuestRequestItems(texts->Fetch());

    if (QueryResult texts = CharacterDatabase.Query("SELECT ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, "
        "EmoteDelay4, RewardText FROM procedurality_quest_offer_reward WHERE ID = {}", questId))
        quest->LoadQuestOfferReward(texts->Fetch());

    return quest;
}

void ProceduralityMgr::Register(ProceduralityBounty const& bounty, Quest* quest)
{
    ProceduralityBounty& stored = _bounties[bounty.QuestId] = bounty;
    stored.Offered = false;

    // Wanted rewards: the saved quest names base items; with affixed rewards, show each as a preview instead.
    // RollRewardItem maps the preview back to its base at turn-in.
    if (stored.Kind == PROCEDURALITY_WANTED && _affixRewards)
    {
        auto preview = [&](uint32& item)
        {
            if (uint32 entry = item ? sForgeMgr->CreatePreview(sObjectMgr->GetItemTemplate(item), stored.RareLevel,
                "Its affixes are rolled when you turn in the quest.") : 0)
            {
                item = entry;
                stored.Previews.push_back(entry);
            }
        };
        for (uint32& item : quest->RewardChoiceItemId)
            preview(item);
        for (uint32& item : quest->RewardItemId)
            preview(item);
    }

    sObjectMgr->AddGeneratedQuest(quest);
    sObjectMgr->GetCreatureQuestInvolvedRelationMap()->emplace(bounty.GiverEntry, bounty.QuestId);
    ApplyPOIs(stored);
}

void ProceduralityMgr::SetOffered(ProceduralityBounty& bounty, bool offered)
{
    QuestRelations* starters = sObjectMgr->GetCreatureQuestRelationMap();
    auto bounds = starters->equal_range(bounty.GiverEntry);
    auto itr = std::find_if(bounds.first, bounds.second, [&](auto const& rel) { return rel.second == bounty.QuestId; });

    if (offered && itr == bounds.second)
        starters->emplace(bounty.GiverEntry, bounty.QuestId);
    else if (!offered && itr != bounds.second)
        starters->erase(itr);

    bounty.Offered = offered;
}

void ProceduralityMgr::ApplyPOIs(ProceduralityBounty const& bounty) const
{
    auto wma = _worldMapAreaByZone.find(bounty.ZoneId);
    uint32 const worldMapArea = wma != _worldMapAreaByZone.end() ? wma->second : 0;

    QuestPOIVector pois;

    // Objective 0 (RequiredNpcOrGo1): an octagon around the bounty area
    QuestPOI area(0, 0, bounty.MapId, worldMapArea, 0, 0, 1);
    for (uint32 i = 0; i < 8; ++i)
    {
        float const angle = float(M_PI) * 2.0f * i / 8.0f;
        area.points.emplace_back(int32(bounty.X + bounty.Radius * std::cos(angle)), int32(bounty.Y + bounty.Radius * std::sin(angle)));
    }
    pois.push_back(std::move(area));

    // Turn-in (-1): the giver
    if (CreatureData const* giver = sObjectMgr->GetCreatureData(bounty.GiverSpawnId))
    {
        QuestPOI turnIn(1, -1, bounty.MapId, worldMapArea, 0, 0, 1);
        turnIn.points.emplace_back(int32(giver->posX), int32(giver->posY));
        pois.push_back(std::move(turnIn));
    }

    sObjectMgr->SetQuestPOIs(bounty.QuestId, std::move(pois));
}

void ProceduralityMgr::ReapplyQuestData()
{
    sObjectMgr->ReserveQuestTemplates(LAST_QUEST_ID);

    for (auto& [questId, bounty] : _bounties)
    {
        QuestRelations* enders = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
        auto bounds = enders->equal_range(bounty.GiverEntry);
        if (std::none_of(bounds.first, bounds.second, [&](auto const& rel) { return rel.second == questId; }))
            enders->emplace(bounty.GiverEntry, questId);

        SetOffered(bounty, bounty.Offered);
        ApplyPOIs(bounty);
    }
}

// The "!" / "?" over the giver for real players nearby
void ProceduralityMgr::RefreshGiverMarkers(ProceduralityBounty const& bounty) const
{
    Map* map = sMapMgr->FindBaseNonInstanceMap(bounty.MapId);
    if (!map)
        return;

    auto creatures = map->GetCreatureBySpawnIdStore().equal_range(bounty.GiverSpawnId);
    for (auto itr = creatures.first; itr != creatures.second; ++itr)
    {
        Creature* giver = itr->second;
        if (!giver->IsInWorld())
            continue;

        for (MapReference const& ref : map->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (IsRealPlayer(player) && player->IsInWorld() && player->IsWithinDist(giver, MARKER_REFRESH_YARDS))
                player->PlayerTalkClass->SendQuestGiverStatus(player->GetQuestDialogStatus(giver), giver->GetGUID());
        }
    }
}

void ProceduralityMgr::OnCreatureDied(Creature const* creature)
{
    if (!creature->GetSpawnId())
        return;

    Map* map = creature->GetMap();
    if (map->Instanceable())
        return;

    auto grid = _activity.find(map->GetId());
    if (grid == _activity.end())
        return;

    time_t const now = GameTime::GetGameTime().count();
    ActivityCell& cell = grid->second[CellKey(creature->GetPositionX(), creature->GetPositionY())];
    cell.Kills = cell.Kills * std::exp2(-float(now - cell.Stamp) / _halfLifeSeconds) + 1.0f;
    cell.Stamp = now;
}

float ProceduralityMgr::ActivityAround(uint32 mapId, float x, float y, float radius) const
{
    auto grid = _activity.find(mapId);
    if (grid == _activity.end())
        return 0.0f;

    time_t const now = GameTime::GetGameTime().count();
    uint32 const lo = CellKey(x - radius, y - radius);
    uint32 const hi = CellKey(x + radius, y + radius);
    float kills = 0.0f;
    for (uint32 cx = lo >> 10; cx <= hi >> 10; ++cx)
    {
        for (uint32 cy = lo & 1023; cy <= (hi & 1023); ++cy)
        {
            auto cell = grid->second.find((cx << 10) | cy);
            if (cell != grid->second.end())
                kills += cell->second.Kills * std::exp2(-float(now - cell->second.Stamp) / _halfLifeSeconds);
        }
    }
    return kills;
}

bool ProceduralityMgr::PassesKillCredit(Player const* player, Quest const* quest, uint32 killedEntry, ObjectGuid killed) const
{
    auto itr = _bounties.find(quest->GetQuestId());
    if (itr == _bounties.end())
        return true;

    ProceduralityBounty const& bounty = itr->second;
    if (bounty.Kind == PROCEDURALITY_WANTED || killedEntry != bounty.TargetEntry)
        return true;   // the rare's entry is its own; other kills don't touch the objective

    Creature const* creature = killed && player->GetMapId() == bounty.MapId ? player->GetMap()->GetCreature(killed) : nullptr;
    float x = creature ? creature->GetPositionX() : player->GetPositionX();
    float y = creature ? creature->GetPositionY() : player->GetPositionY();
    float const deathDist = std::sqrt(Dist2(x, y, bounty.X, bounty.Y));
    if (player->GetMapId() == bounty.MapId)
    {
        // The camp's own spawns count wherever they died (they wander and get chased out); others count inside the
        // area plus a margin for that wander. Field creatures (Plainstriders, boars) spawn right up to the edge.
        if (deathDist <= bounty.Radius + KILL_CREDIT_MARGIN)
            return true;
        if (creature)
        {
            float sx, sy, sz;
            creature->GetRespawnPosition(sx, sy, sz);
            if (Dist2(sx, sy, bounty.X, bounty.Y) <= bounty.Radius * bounty.Radius)
                return true;
        }
    }

    // Otherwise the kill vanishes from the tracker with no hint why; say where the bounty's area is
    if (IsRealPlayer(player))
        ChatHandler(player->GetSession()).PSendSysMessage("|cffffd000Bounty:|r that kill doesn't count for {}: only inside the marked area at {}{}.",
            quest->GetTitle(), AreaName(bounty.AreaId ? bounty.AreaId : bounty.ZoneId),
            player->GetMapId() == bounty.MapId ? Acore::StringFormat(", {} yd from here", int32(deathDist - bounty.Radius)) : "");
    return false;
}

std::string ProceduralityMgr::PickText(std::string const& kind, uint32 creatureType,
    std::unordered_map<std::string, std::string> const& vars) const
{
    static std::unordered_map<std::string, std::string> const fallback = {
        { "title", "Bounty: $targets" },
        { "objectives", "Kill $count $targets near $area, then return to $giver." },
        { "details", "The $targets near $area are getting bold. Thin them out and I'll see you paid." },
        { "request", "Have you dealt with the $targets?" },
        { "reward", "Good work. That should keep them quiet for a while." },
        { "say", "Bounty posted: $targets near $area. Speak to me if you want the work." },
        { "wanted_title", "Wanted: $rare" },
        { "wanted_objectives", "Kill $rare near $area, then return to $giver." },
        { "wanted_details", "$rare has been seen near $area. Bring it down and the reward is yours." },
        { "wanted_request", "Is $rare dead?" },
        { "wanted_reward", "So $rare is finally dead. Well done, $N." },
        { "wanted_say", "Wanted: $rare, last seen near $area. There's a reward." },
        { "rare_pattern", "$prefix$suffix the $epithet" },
        { "rare_prefix", "Grim" },
        { "rare_suffix", "fang" },
        { "rare_epithet", "Cruel" },
    };

    std::vector<Text const*> candidates;
    uint32 total = 0;
    if (auto itr = _texts.find(kind); itr != _texts.end())
    {
        for (Text const& text : itr->second)
        {
            if (text.Weight && (!text.TypeMask || (creatureType < 32 && (text.TypeMask & (1u << creatureType)))))
            {
                candidates.push_back(&text);
                total += text.Weight;
            }
        }
    }

    std::string chosen;
    if (total)
    {
        uint32 roll = urand(1, total);
        for (Text const* text : candidates)
        {
            if (roll <= text->Weight)
            {
                chosen = text->Value;
                break;
            }
            roll -= text->Weight;
        }
    }
    else if (auto itr = fallback.find(kind); itr != fallback.end())
        chosen = itr->second;

    return Substitute(chosen, vars);
}

bool ProceduralityMgr::Post(Player* player, ProceduralityKind kind, bool force, std::string& result)
{
    if (!player || !player->IsInWorld())
    {
        result = "Not in the world.";
        return false;
    }

    Map* map = player->GetMap();
    if (map->Instanceable() || !_activity.contains(map->GetId()))
    {
        result = "Incidents only happen on the continents.";
        return false;
    }

    time_t const now = GameTime::GetGameTime().count();
    uint8 const level = player->GetLevel();
    FactionTemplateEntry const* playerFaction = player->GetFactionTemplateEntry();
    if (!playerFaction)
    {
        result = "No faction.";
        return false;
    }

    if (!_loaded)
    {
        result = "Procedurality didn't load (see the startup log).";
        return false;
    }

    bool const wanted = kind == PROCEDURALITY_WANTED;
    auto liveOfKind = [&](auto&& filter)
    {
        uint32 count = 0;
        for (auto const& [questId, bounty] : _bounties)
            if (bounty.Kind == kind && filter(bounty))
                ++count;
        return count;
    };

    if (!force)
    {
        if (_bounties.size() >= _maxActive || (wanted && liveOfKind([](auto const&) { return true; }) >= _wantedMaxActive))
        {
            result = "World-wide budget is full.";
            return false;
        }

        if (auto last = _lastPostedFor.find(player->GetGUID().GetCounter()); last != _lastPostedFor.end() && now < last->second + time_t(_playerCooldown))
        {
            result = "Player cooldown.";
            return false;
        }

        // One that already suits this player nearby? (Not one they turned in: it's non-repeatable. One they
        // dropped still counts, as they can take it again.)
        for (auto const& [questId, bounty] : _bounties)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (bounty.Offered && quest && bounty.MapId == map->GetId() && (quest->GetAllowableRaces() & player->getRaceMask())
                && !player->GetQuestRewardStatus(questId)
                && player->GetExactDist2dSq(bounty.X, bounty.Y) <= _nearbyYards * _nearbyYards
                && int32(bounty.Level) >= int32(level) - 3 && bounty.Level <= level + 2)
            {
                CreatureTemplate const* giver = sObjectMgr->GetCreatureTemplate(bounty.GiverEntry);
                result = Acore::StringFormat("One that fits is already nearby: {} ({}) from {}.", quest->GetTitle(), questId,
                    giver ? giver->Name : "?");
                return false;
            }
        }
    }

    if (wanted && _nextRareEntry > LAST_RARE_ENTRY)
    {
        result = "Out of reserved rare entries.";
        return false;
    }

    // --- Camp: same-entry spawns that are eligible, in level, and not friendly to the player ---
    struct Spawn
    {
        float x, y, z;
    };
    std::unordered_map<uint32, std::vector<Spawn>> byEntry;

    float const px = player->GetPositionX();
    float const py = player->GetPositionY();
    GridCoord const a = Acore::ComputeGridCoord(px - _searchYards, py - _searchYards);
    GridCoord const b = Acore::ComputeGridCoord(px + _searchYards, py + _searchYards);
    for (uint32 gx = std::min(a.x_coord, b.x_coord); gx <= std::max(a.x_coord, b.x_coord); ++gx)
    {
        for (uint32 gy = std::min(a.y_coord, b.y_coord); gy <= std::max(a.y_coord, b.y_coord); ++gy)
        {
            CellObjectGuids const& guids = sObjectMgr->GetGridObjectGuids(map->GetId(), map->GetSpawnMode(), GridCoord(gx, gy).GetId());
            for (ObjectGuid::LowType spawnId : guids.creatures)
            {
                CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
                if (!data || !(data->phaseMask & player->GetPhaseMask()) || Dist2(data->posX, data->posY, px, py) > _searchYards * _searchYards)
                    continue;

                if (!Map::IsLocalRespawnEligible(data))
                    continue;

                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(data->id);
                if (!cInfo || cInfo->maxlevel + 2 < level || cInfo->minlevel > level + 2)
                    continue;

                // A rare needs a loot template: Unit::Kill only fills loot (and so the rare's drop) when there is one
                if (wanted && !cInfo->lootid)
                    continue;

                FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(cInfo->faction);
                if (!faction || faction->IsFriendlyTo(*playerFaction))
                    continue;

                byEntry[data->id].push_back({ data->posX, data->posY, data->posZ });
            }
        }
    }

    struct Camp
    {
        uint32 entry = 0;
        float x = 0, y = 0, z = 0, radius = 0;
        Spawn middle{};   // the member nearest the centre
        uint32 size = 0;
        float score = 0;
    };
    Camp best;
    bool found = false;
    float const campSq = _campYards * _campYards;

    for (auto const& [entry, spawns] : byEntry)
    {
        if (spawns.size() < _minCampSize)
            continue;

        // The spawn with the most same-entry neighbours anchors the camp
        std::size_t anchor = 0;
        uint32 anchorCount = 0;
        for (std::size_t i = 0; i < spawns.size(); ++i)
        {
            uint32 count = 0;
            for (Spawn const& other : spawns)
                if (Dist2(spawns[i].x, spawns[i].y, other.x, other.y) <= campSq)
                    ++count;
            if (count > anchorCount)
            {
                anchor = i;
                anchorCount = count;
            }
        }

        if (anchorCount < _minCampSize)
            continue;

        Camp camp;
        camp.entry = entry;
        camp.size = anchorCount;
        for (Spawn const& s : spawns)
        {
            if (Dist2(spawns[anchor].x, spawns[anchor].y, s.x, s.y) <= campSq)
            {
                camp.x += s.x / anchorCount;
                camp.y += s.y / anchorCount;
            }
        }

        // Height and radius from the members; the member nearest the centre gives the height
        float nearest = std::numeric_limits<float>::max();
        for (Spawn const& s : spawns)
        {
            if (Dist2(spawns[anchor].x, spawns[anchor].y, s.x, s.y) > campSq)
                continue;
            float const d = Dist2(camp.x, camp.y, s.x, s.y);
            camp.radius = std::max(camp.radius, std::sqrt(d));
            if (d < nearest)
            {
                nearest = d;
                camp.middle = s;
                camp.z = s.z;
            }
        }
        camp.radius = std::clamp(camp.radius + 15.0f, 30.0f, 130.0f);

        float const dist = std::sqrt(Dist2(camp.x, camp.y, px, py));
        if (dist > _searchYards || (!force && dist < _minYards))
            continue;

        // Not on top of a live incident for the same creature
        bool taken = false;
        for (auto const& [questId, bounty] : _bounties)
            if (bounty.TargetEntry == entry && bounty.MapId == map->GetId() && Dist2(bounty.X, bounty.Y, camp.x, camp.y) <= 4 * campSq)
                taken = true;
        if (taken)
            continue;

        // Prefer quiet camps a short walk away
        camp.score = ActivityAround(map->GetId(), camp.x, camp.y, camp.radius) * 4.0f + std::abs(dist - 300.0f) / 100.0f + frand(0.0f, 1.5f);
        if (!found || camp.score < best.score)
        {
            best = camp;
            found = true;
        }
    }

    if (!found)
    {
        result = "No camp in range fits.";
        return false;
    }

    uint32 const zoneId = map->GetZoneId(player->GetPhaseMask(), best.x, best.y, best.z);
    uint32 const areaId = map->GetAreaId(player->GetPhaseMask(), best.x, best.y, best.z);

    if (!force)
    {
        uint32 inZone = 0;
        for (auto const& [questId, bounty] : _bounties)
            if (bounty.ZoneId == zoneId)
                ++inZone;
        if (inZone >= _maxPerZone || (wanted && liveOfKind([&](auto const& b) { return b.ZoneId == zoneId; }) >= _wantedMaxPerZone))
        {
            result = "Zone budget is full.";
            return false;
        }
    }

    // --- Giver: a friendly quest giver in the same zone as the camp ---
    struct Giver
    {
        CreatureData const* data;
        float dist;
    };
    std::vector<Giver> givers;
    GridCoord const ga = Acore::ComputeGridCoord(best.x - _giverYards, best.y - _giverYards);
    GridCoord const gb = Acore::ComputeGridCoord(best.x + _giverYards, best.y + _giverYards);
    for (uint32 gx = std::min(ga.x_coord, gb.x_coord); gx <= std::max(ga.x_coord, gb.x_coord); ++gx)
    {
        for (uint32 gy = std::min(ga.y_coord, gb.y_coord); gy <= std::max(ga.y_coord, gb.y_coord); ++gy)
        {
            CellObjectGuids const& guids = sObjectMgr->GetGridObjectGuids(map->GetId(), map->GetSpawnMode(), GridCoord(gx, gy).GetId());
            for (ObjectGuid::LowType spawnId : guids.creatures)
            {
                CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
                if (!data || !(data->phaseMask & player->GetPhaseMask()))
                    continue;

                float const d = Dist2(data->posX, data->posY, best.x, best.y);
                if (d > _giverYards * _giverYards)
                    continue;

                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(data->id);
                if (!cInfo || !(cInfo->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
                    continue;

                auto starters = sObjectMgr->GetCreatureQuestRelationBounds(data->id);
                std::size_t const quests = std::distance(starters.first, starters.second);
                if (!quests || quests >= MAX_GIVER_QUESTS)
                    continue;

                FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(cInfo->faction);
                if (!faction || !faction->IsFriendlyTo(*playerFaction) || faction->IsHostileToPlayers())
                    continue;

                uint32 live = 0;
                for (auto const& [questId, bounty] : _bounties)
                    if (bounty.GiverEntry == data->id)
                        ++live;
                if (live >= _maxPerGiver)
                    continue;

                // Nearest to the player first (they are the one being offered the work), then to the camp
                givers.push_back({ data, std::sqrt(Dist2(data->posX, data->posY, px, py)) + 0.5f * std::sqrt(d) });
            }
        }
    }

    std::sort(givers.begin(), givers.end(), [](Giver const& l, Giver const& r) { return l.dist < r.dist; });
    CreatureData const* giver = nullptr;
    for (Giver const& g : givers)
    {
        if (map->GetZoneId(g.data->phaseMask, g.data->posX, g.data->posY, g.data->posZ) == zoneId)
        {
            giver = g.data;
            break;
        }
    }

    CreatureTemplate const* target = sObjectMgr->GetCreatureTemplate(best.entry);
    if (!giver)
    {
        result = Acore::StringFormat("No quest giver in {} for a camp of {}.", AreaName(zoneId), target->Name);
        return false;
    }

    // --- The incident and its quest ---
    CreatureTemplate const* giverInfo = sObjectMgr->GetCreatureTemplate(giver->id);

    ProceduralityBounty bounty;
    bounty.Kind = kind;
    bounty.QuestId = _nextQuestId;
    bounty.MapId = map->GetId();
    bounty.ZoneId = zoneId;
    bounty.AreaId = areaId;
    bounty.X = best.x;
    bounty.Y = best.y;
    bounty.Z = best.z;
    bounty.Radius = best.radius;
    bounty.TargetEntry = best.entry;
    bounty.Level = uint8(std::clamp<int32>((target->minlevel + target->maxlevel + 1) / 2, 1, DEFAULT_MAX_LEVEL));
    bounty.GiverSpawnId = giver->spawnId;
    bounty.GiverEntry = giver->id;
    bounty.ForPlayer = player->GetGUID().GetCounter();
    bounty.Created = now;
    bounty.OfferUntil = now + _offerSeconds;
    bounty.RetireAt = bounty.OfferUntil + _graceSeconds;

    if (bounty.QuestId > LAST_QUEST_ID)
    {
        result = "Out of generated quest IDs.";
        return false;
    }

    if (wanted)
    {
        // A level or two above its kind, standing at the camp's middle spawn
        bounty.KillCount = 1;
        bounty.RareEntry = _nextRareEntry;
        bounty.RareLevel = uint8(std::clamp<int32>(std::min<int32>(target->maxlevel + 2, level + 2), target->maxlevel, DEFAULT_MAX_LEVEL));
        bounty.Level = bounty.RareLevel;
        bounty.X = best.middle.x;
        bounty.Y = best.middle.y;
        bounty.Z = best.middle.z;
        bounty.Radius = 40.0f;

        for (uint32 attempt = 0; attempt < 8 && bounty.RareName.empty(); ++attempt)
        {
            std::string name = RareName(target->type);
            bool const taken = std::any_of(_bounties.begin(), _bounties.end(), [&](auto const& b) { return b.second.RareName == name; });
            if (!taken && !name.empty() && name.size() < 64)
                bounty.RareName = name;
        }
        if (bounty.RareName.empty() || !FillRareTemplate(bounty.RareEntry, bounty.TargetEntry, bounty.RareName, bounty.RareLevel))
        {
            result = "Couldn't build the rare (name or reserved creature_template row missing).";
            return false;
        }
    }
    else
    {
        // Fewer kills while levels come quickly: at most 8 below level 10, 12 below 20, then 15
        uint32 const maxKills = level < 10 ? 8 : level < 20 ? 12 : 15;
        bounty.KillCount = std::clamp<uint32>(uint32(std::lround(best.size * 1.5f)), std::min<uint32>(6, maxKills), maxKills);
    }

    std::string const areaName = areaId && areaId != zoneId ? AreaName(areaId) : AreaName(zoneId);
    std::unordered_map<std::string, std::string> const vars = {
        { "target", target->Name },
        { "targets", Plural(target->Name) },
        { "count", std::to_string(bounty.KillCount) },
        { "area", WithArticle(areaName) },
        { "zone", AreaName(zoneId) },
        { "giver", giverInfo->Name },
        { "rare", bounty.RareName },
    };

    uint32 const races = player->GetTeamId() == TEAM_ALLIANCE ? RaceMgr::GetAllianceRaceMask() : RaceMgr::GetHordeRaceMask();
    Quest* quest = BuildQuest(bounty, vars, target->type, races);
    if (!quest)
    {
        result = "Couldn't save the quest (see the error log).";
        return false;
    }

    ++_nextQuestId;
    if (wanted)
        ++_nextRareEntry;
    _lastPostedFor[bounty.ForPlayer] = now;
    Register(bounty, quest);
    ProceduralityBounty& stored = _bounties[bounty.QuestId];
    SetOffered(stored, true);
    if (wanted)
        SpawnRare(stored);
    RefreshGiverMarkers(stored);
    Announce(stored, PickText(wanted ? "wanted_say" : "say", target->type, vars));

    if (wanted)
        result = Acore::StringFormat("Wanted {} \"{}\": {} (rare {}, level {}, from {}) near {} ({:.0f}, {:.0f}), giver {} ({}).",
            bounty.QuestId, quest->GetTitle(), bounty.RareName, bounty.RareEntry, bounty.RareLevel, target->Name, areaName,
            bounty.X, bounty.Y, giverInfo->Name, giver->spawnId);
    else
        result = Acore::StringFormat("Bounty {} \"{}\": {} x{} (level {}) near {} ({:.0f}, {:.0f}, r {:.0f}), giver {} ({}).",
            bounty.QuestId, quest->GetTitle(), target->Name, bounty.KillCount, bounty.Level, areaName, bounty.X, bounty.Y, bounty.Radius,
            giverInfo->Name, giver->spawnId);
    LOG_INFO("module.procedurality", "Posted for {}: {}", player->GetName(), result);
    Debug(1, Acore::StringFormat("posted for {}: {}", player->GetName(), result));
    return true;
}

Quest* ProceduralityMgr::BuildQuest(ProceduralityBounty const& bounty, std::unordered_map<std::string, std::string> const& vars,
    uint32 creatureType, uint32 races)
{
    bool const wanted = bounty.Kind == PROCEDURALITY_WANTED;
    auto text = [&](std::string const& kind)
    {
        std::string s = PickText(wanted ? "wanted_" + kind : kind, creatureType, vars);
        CharacterDatabase.EscapeString(s);
        return s;
    };

    std::string const title = text("title");
    std::string const objectives = text("objectives");
    std::string const details = text("details");
    std::string const request = text("request");
    std::string const reward = text("reward");
    std::string completion = Substitute("Return to $giver.", vars);
    CharacterDatabase.EscapeString(completion);
    std::string rareName = bounty.RareName;
    CharacterDatabase.EscapeString(rareName);

    // Wanted: twice the money, a richer XP column, and "Suggested players: 2" (the rare has several times a
    // normal mob's health)
    uint32 const money = uint32(5.0f * std::pow(float(bounty.Level), 1.87f) * _moneyRate * (wanted ? 2.0f : 1.0f));
    uint8 const minLevel = uint8(std::max<int32>(1, int32(bounty.Level) - 5));
    uint32 const objective = wanted ? bounty.RareEntry : bounty.TargetEntry;
    // Wanted: a choice of one base per category, blue if the quest rolled blue and the category has one at this
    // level, else green; with affixed rewards, RollRewardItem gives each player an affixed version of their pick
    std::vector<uint32> choices;
    if (wanted)
    {
        std::size_t const first = roll_chance_f(_rareBlueChance) ? 1 : 0;
        for (std::size_t category = 0; category < REWARD_CATEGORIES; ++category)
            for (std::size_t quality : { first, 1 - first })
                if (uint32 item = PickRewardBase(quality, category, bounty.RareLevel))
                {
                    choices.push_back(item);
                    break;
                }
    }
    choices.resize(QUEST_REWARD_CHOICES_COUNT, 0);

    CharacterDatabase.DirectExecute("INSERT INTO procedurality_quest_template (ID, QuestType, QuestLevel, MinLevel, QuestSortID, SuggestedGroupNum, "
        "RewardXPDifficulty, RewardMoney, Flags, AllowableRaces, POIContinent, POIx, POIy, LogTitle, LogDescription, QuestDescription, "
        "AreaDescription, QuestCompletionLog, RequiredNpcOrGo1, RequiredNpcOrGoCount1, RewardChoiceItemID1, RewardChoiceItemQuantity1, "
        "RewardChoiceItemID2, RewardChoiceItemQuantity2, RewardChoiceItemID3, RewardChoiceItemQuantity3, RewardChoiceItemID4, "
        "RewardChoiceItemQuantity4, RewardChoiceItemID5, RewardChoiceItemQuantity5, RewardChoiceItemID6, RewardChoiceItemQuantity6) VALUES "
        "({}, 2, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}', '{}', '{}', '', '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        bounty.QuestId, bounty.Level, minLevel, bounty.ZoneId, wanted ? 2 : 0, wanted ? _wantedXpDifficulty : _xpDifficulty, money,
        uint32(QUEST_FLAGS_SHARABLE), races, bounty.MapId, bounty.X, bounty.Y, title, objectives, details, completion, objective,
        bounty.KillCount, choices[0], choices[0] ? 1 : 0, choices[1], choices[1] ? 1 : 0, choices[2], choices[2] ? 1 : 0,
        choices[3], choices[3] ? 1 : 0, choices[4], choices[4] ? 1 : 0, choices[5], choices[5] ? 1 : 0);
    CharacterDatabase.DirectExecute("INSERT INTO procedurality_quest_request_items (ID, CompletionText) VALUES ({}, '{}')", bounty.QuestId, request);
    CharacterDatabase.DirectExecute("INSERT INTO procedurality_quest_offer_reward (ID, RewardText) VALUES ({}, '{}')", bounty.QuestId, reward);
    CharacterDatabase.DirectExecute("INSERT INTO procedurality_bounty (quest_id, map, zone, area, x, y, z, radius, target_entry, kill_count, level, "
        "giver_spawn, giver_entry, for_player, created, offer_until, retire_at, retired, kind, rare_entry, rare_name, rare_level) VALUES "
        "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, {}, {}, '{}', {})",
        bounty.QuestId, bounty.MapId, bounty.ZoneId, bounty.AreaId, bounty.X, bounty.Y, bounty.Z, bounty.Radius, bounty.TargetEntry,
        bounty.KillCount, bounty.Level, bounty.GiverSpawnId, bounty.GiverEntry, bounty.ForPlayer, uint32(bounty.Created),
        uint32(bounty.OfferUntil), uint32(bounty.RetireAt), bounty.Kind, bounty.RareEntry, rareName, bounty.RareLevel);

    Quest* quest = LoadQuest(bounty.QuestId);
    if (!quest)
        LOG_ERROR("module.procedurality", "Generated quest {} didn't read back from procedurality_quest_template", bounty.QuestId);
    return quest;
}

// --- Wanted rares ---

// Names from procedurality_text parts: rare_pattern (e.g. "$prefix$suffix the $epithet", "$first $last") filled with
// rare_prefix / rare_suffix / rare_first / rare_last / rare_epithet, each picked for the creature type
std::string ProceduralityMgr::RareName(uint32 creatureType) const
{
    std::unordered_map<std::string, std::string> parts;
    for (char const* part : { "prefix", "suffix", "first", "last", "epithet" })
        parts[part] = PickText(std::string("rare_") + part, creatureType, {});

    std::string name = Substitute(PickText("rare_pattern", creatureType, {}), parts);
    if (name.find('$') != std::string::npos)
        return "";
    return name;
}

// Rebuild a reserved creature_template row as a rare copy of base: own name, <Wanted>, rare rank, a fixed level,
// more health and damage, the base's loot and gold (the affixed reward comes from the quest, so killing the rare
// again and again pays no better than its kind), models, spells and faction. KillCredit1 = base, so the base's own quests
// still count it. World thread only; the row is unused until the rare is summoned.
bool ProceduralityMgr::FillRareTemplate(uint32 entry, uint32 baseEntry, std::string const& name, uint8 level) const
{
    CreatureTemplate const* slot = sObjectMgr->GetCreatureTemplate(entry);
    CreatureTemplate const* base = sObjectMgr->GetCreatureTemplate(baseEntry);
    if (!IsRareEntry(entry) || !slot || !base)
    {
        LOG_ERROR("module.procedurality", "Can't build rare {} from {}: {} missing", entry, baseEntry, slot ? "base" : "reserved creature_template row");
        return false;
    }

    CreatureTemplate* rare = const_cast<CreatureTemplate*>(slot);
    *rare = *base;
    rare->Entry = entry;
    for (uint32& difficulty : rare->DifficultyEntry)
        difficulty = 0;
    rare->Name = name;
    rare->SubName = "Wanted";
    rare->minlevel = level;
    rare->maxlevel = level;
    rare->rank = CREATURE_ELITE_RARE;
    rare->ModHealth = base->ModHealth * _rareHealth;
    // The loaded DamageModifier already carries the base rank's rate (CheckCreatureTemplate)
    rare->DamageModifier = base->DamageModifier / Creature::_GetDamageMod(base->rank) * Creature::_GetDamageMod(CREATURE_ELITE_RARE) * _rareDamage;
    rare->KillCredit[0] = base->Entry;
    for (uint32 i = 1; i < MAX_KILL_CREDIT; ++i)
        rare->KillCredit[i] = 0;
    rare->npcflag = 0;
    rare->GossipMenuId = 0;
    rare->AIName.clear();
    rare->ScriptID = 0;
    rare->InitializeQueryData();
    return true;
}

void ProceduralityMgr::SpawnRare(ProceduralityBounty& bounty)
{
    Map* map = sMapMgr->FindBaseNonInstanceMap(bounty.MapId);
    if (!map)
        return;

    TempSummon* rare = map->SummonCreature(bounty.RareEntry, Position(bounty.X, bounty.Y, bounty.Z, frand(0.0f, 2.0f * float(M_PI))));
    if (!rare)
    {
        LOG_ERROR("module.procedurality", "Couldn't summon rare {} for {}", bounty.RareEntry, bounty.QuestId);
        bounty.RareDiedAt = GameTime::GetGameTime().count();
        return;
    }

    // The base's weapons (creature_equip_template is keyed by entry), a little bigger, wandering its camp
    int8 equipId = 1;
    if (EquipmentInfo const* equipment = sObjectMgr->GetEquipmentInfo(bounty.TargetEntry, equipId))
        for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
            rare->SetVirtualItem(i, equipment->ItemEntry[i]);
    rare->SetObjectScale(rare->GetObjectScale() * 1.15f);
    rare->SetWanderDistance(8.0f);
    rare->SetDefaultMovementType(RANDOM_MOTION_TYPE);
    rare->GetMotionMaster()->Initialize();

    bounty.RareGuid = rare->GetGUID();
    bounty.RareDiedAt = 0;
    Debug(1, Acore::StringFormat("rare {} ({}) up at {:.0f} {:.0f} {:.0f}, level {}", bounty.RareName, bounty.QuestId,
        bounty.X, bounty.Y, bounty.Z, bounty.RareLevel));
}

void ProceduralityMgr::Debug(uint8 level, std::string const& text) const
{
    if (_debug < level)
        return;

    for (auto const& [accountId, session] : sWorldSessionMgr->GetAllSessions())
        if (session->GetSecurity() >= SEC_GAMEMASTER && !session->IsSocketClosed() && session->GetPlayer() && session->GetPlayer()->IsInWorld())
            ChatHandler(session).PSendSysMessage("|cff66ccff[procedurality]|r {}", text);
}

void ProceduralityMgr::DespawnRare(ProceduralityBounty& bounty) const
{
    if (!bounty.RareGuid)
        return;

    if (Map* map = sMapMgr->FindBaseNonInstanceMap(bounty.MapId))
        if (Creature* creature = map->GetCreature(bounty.RareGuid))
            if (TempSummon* summon = creature->ToTempSummon())
                summon->UnSummon();
    bounty.RareGuid.Clear();
}

// Each tick: note deaths, and summon the rare again Procedurality.Wanted.RespawnMinutes after it died or vanished
// (summons don't survive a restart, so after one it comes back at once).
void ProceduralityMgr::MaintainRares(time_t now)
{
    for (auto& [questId, bounty] : _bounties)
    {
        if (bounty.Kind != PROCEDURALITY_WANTED)
            continue;

        Map* map = sMapMgr->FindBaseNonInstanceMap(bounty.MapId);
        if (!map)
            continue;

        if (bounty.RareGuid)
        {
            if (Creature* creature = map->GetCreature(bounty.RareGuid))
            {
                if (!creature->IsAlive() && !bounty.RareDiedAt)
                {
                    bounty.RareDiedAt = now;
                    Debug(1, Acore::StringFormat("rare {} ({}) died; back in {} min", bounty.RareName, questId, _rareRespawnSeconds / MINUTE));
                }
                continue;
            }

            // Corpse gone (the summon unsummons itself once its corpse decays)
            if (!bounty.RareDiedAt)
                bounty.RareDiedAt = now;
            bounty.RareGuid.Clear();
        }

        if (!bounty.RareDiedAt || now >= bounty.RareDiedAt + time_t(_rareRespawnSeconds))
            SpawnRare(bounty);
    }
}

// Affixable greens and blues by category and item level, for Wanted quest rewards
void ProceduralityMgr::LoadRareBases()
{
    for (auto& byQuality : _rewardBases)
        for (auto& byCategory : byQuality)
            for (auto& entries : byCategory)
                entries.clear();

    for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
    {
        if ((proto.Quality != ITEM_QUALITY_UNCOMMON && proto.Quality != ITEM_QUALITY_RARE) || !proto.ItemLevel || proto.ItemLevel > 255)
            continue;
        // Affixed rewards are rolled from the base, so it has to take affixes
        if (_affixRewards && !sForgeMgr->IsEligible(&proto))
            continue;

        std::size_t category;
        if (proto.InventoryType == INVTYPE_CLOAK || proto.InventoryType == INVTYPE_NECK || proto.InventoryType == INVTYPE_FINGER)
            category = 4;
        else if (proto.Class == ITEM_CLASS_WEAPON)
            category = 5;
        else if (proto.Class == ITEM_CLASS_ARMOR && proto.SubClass >= ITEM_SUBCLASS_ARMOR_CLOTH && proto.SubClass <= ITEM_SUBCLASS_ARMOR_PLATE)
            category = proto.SubClass - ITEM_SUBCLASS_ARMOR_CLOTH;
        else
            continue;

        _rewardBases[proto.Quality == ITEM_QUALITY_RARE ? 1 : 0][category][proto.ItemLevel].push_back(entry);
    }
}

void ProceduralityMgr::PushRewardTemplates(Player* player, Quest const* quest) const
{
    auto itr = _bounties.find(quest->GetQuestId());
    if (itr == _bounties.end() || !player || !player->GetSession())
        return;

    for (uint32 entry : itr->second.Previews)
        sForgeMgr->SendTemplate(player, entry);
}

// A base of quality (0 green, 1 blue) and category within 8 item levels of itemLevel, nearest first; 0 if none
uint32 ProceduralityMgr::PickRewardBase(std::size_t quality, std::size_t category, uint8 itemLevel) const
{
    for (int32 delta = 0; delta <= 8; ++delta)
    {
        for (int32 ilvl : { int32(itemLevel) - delta, int32(itemLevel) + delta })
        {
            if (ilvl >= 1 && ilvl <= 255 && !_rewardBases[quality][category][ilvl].empty())
                return Acore::Containers::SelectRandomContainerElement(_rewardBases[quality][category][ilvl]);
        }
    }
    return 0;
}

uint32 ProceduralityMgr::RollRewardItem(Player* player, Quest const* quest, uint32 baseItem) const
{
    auto itr = _bounties.find(quest->GetQuestId());
    if (itr == _bounties.end() || itr->second.Kind != PROCEDURALITY_WANTED)
        return baseItem;

    // The quest shows a preview; roll from the base behind it
    if (GeneratedItem const* preview = sForgeMgr->GetGenerated(baseItem); preview && preview->Preview)
        baseItem = preview->BaseEntry;

    // Procedurality.Rewards.Affixes off: the plain base item
    if (!_affixRewards)
        return baseItem;

    ItemTemplate const* base = sObjectMgr->GetItemTemplate(baseItem);
    uint32 const entry = base ? sForgeMgr->Generate(base, itr->second.RareLevel, true) : 0;
    if (!entry)
    {
        LOG_ERROR("module.procedurality", "Couldn't roll a reward from item {} for quest {}; giving the base item", baseItem, quest->GetQuestId());
        return baseItem;
    }

    // The entry may be a recycled one the client still has cached
    sForgeMgr->SendTemplate(player, entry);
    Debug(1, Acore::StringFormat("{} turned in {}: rolled {} ({})", player->GetName(), quest->GetQuestId(),
        sObjectMgr->GetItemTemplate(entry)->Name1, entry));
    return entry;
}

void ProceduralityMgr::Announce(ProceduralityBounty const& bounty, std::string const& say) const
{
    Map* map = sMapMgr->FindBaseNonInstanceMap(bounty.MapId);
    if (!map)
        return;

    auto creatures = map->GetCreatureBySpawnIdStore().equal_range(bounty.GiverSpawnId);
    for (auto itr = creatures.first; itr != creatures.second; ++itr)
    {
        if (itr->second->IsAlive() && itr->second->IsInWorld())
        {
            itr->second->Say(say, LANG_UNIVERSAL);
            break;
        }
    }

    Quest const* quest = sObjectMgr->GetQuestTemplate(bounty.QuestId);
    CreatureTemplate const* giver = sObjectMgr->GetCreatureTemplate(bounty.GiverEntry);
    if (!quest || !giver)
        return;

    for (MapReference const& ref : map->GetPlayers())
    {
        Player* player = ref.GetSource();
        if (IsRealPlayer(player) && player->IsInWorld() && player->GetZoneId() == bounty.ZoneId && (quest->GetAllowableRaces() & player->getRaceMask()))
            ChatHandler(player->GetSession()).PSendSysMessage("|cffffd000New bounty|r from {}: {}", giver->Name, quest->GetTitle());
    }
}

void ProceduralityMgr::RetireRows(uint32 questId) const
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("DELETE FROM character_queststatus WHERE quest = {}", questId);
    trans->Append("DELETE FROM character_queststatus_rewarded WHERE quest = {}", questId);
    trans->Append("DELETE FROM procedurality_quest_template WHERE ID = {}", questId);
    trans->Append("DELETE FROM procedurality_quest_request_items WHERE ID = {}", questId);
    trans->Append("DELETE FROM procedurality_quest_offer_reward WHERE ID = {}", questId);
    trans->Append("UPDATE procedurality_bounty SET retired = 1 WHERE quest_id = {}", questId);
    CharacterDatabase.CommitTransaction(trans);
}

bool ProceduralityMgr::Retire(uint32 questId)
{
    auto itr = _bounties.find(questId);
    if (itr == _bounties.end())
        return false;

    ProceduralityBounty& bounty = itr->second;
    SetOffered(bounty, false);
    DespawnRare(bounty);
    for (uint32 entry : bounty.Previews)
        sForgeMgr->ReleasePreview(entry);

    QuestRelations* enders = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    auto bounds = enders->equal_range(bounty.GiverEntry);
    for (auto rel = bounds.first; rel != bounds.second; ++rel)
    {
        if (rel->second == questId)
        {
            enders->erase(rel);
            break;
        }
    }
    sObjectMgr->RemoveQuestPOIs(questId);

    // Out of every online quest log (bots too), like an abandon
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    for (auto const& [accountId, session] : sWorldSessionMgr->GetAllSessions())
    {
        Player* player = session->GetPlayer();
        if (!player)
            continue;

        uint16 const slot = player->FindQuestSlot(questId);
        if (slot < MAX_QUEST_LOG_SIZE)
        {
            player->TakeQuestSourceItem(questId, false);
            player->AbandonQuest(questId);
            player->RemoveActiveQuest(questId);
            player->SetQuestSlot(slot, 0);
            if (IsRealPlayer(player) && quest)
                ChatHandler(session).PSendSysMessage("|cffffd000Bounty expired:|r {}", quest->GetTitle());
        }
        else if (player->GetQuestStatus(questId) != QUEST_STATUS_NONE)
            player->RemoveActiveQuest(questId, false);

        if (player->IsQuestRewarded(questId))
            player->RemoveRewardedQuest(questId, false);
    }

    RetireRows(questId);
    RefreshGiverMarkers(bounty);
    LOG_INFO("module.procedurality", "Retired bounty {}", questId);
    Debug(1, Acore::StringFormat("retired {}{}", questId, quest ? Acore::StringFormat(" \"{}\"", quest->GetTitle()) : ""));
    _bounties.erase(itr);
    return true;
}

void ProceduralityMgr::Update(uint32 diff)
{
    _timer += diff;
    if (_timer < _intervalMs)
        return;

    _timer = 0;
    Tick();
}

void ProceduralityMgr::Tick()
{
    time_t const now = GameTime::GetGameTime().count();

    std::vector<uint32> retire;
    for (auto& [questId, bounty] : _bounties)
    {
        if (bounty.Offered && now >= bounty.OfferUntil)
        {
            SetOffered(bounty, false);
            RefreshGiverMarkers(bounty);
            Debug(1, Acore::StringFormat("{} no longer offered; turn-in only for {} min", questId, (bounty.RetireAt - now) / MINUTE));
        }
        if (now >= bounty.RetireAt)
            retire.push_back(questId);
    }
    for (uint32 questId : retire)
        Retire(questId);

    MaintainRares(now);

    if (!_enabled)
        return;

    // Incidents follow real players; bots only take part
    std::vector<Player*> players;
    for (auto const& [accountId, session] : sWorldSessionMgr->GetAllSessions())
    {
        Player* player = session->GetPlayer();
        if (IsRealPlayer(player) && player->IsInWorld() && player->IsAlive() && !player->IsGameMaster() && !player->IsInFlight())
            players.push_back(player);
    }
    Acore::Containers::RandomShuffle(players);

    for (Player* player : players)
    {
        // Sometimes a Wanted rare; if none fits (budget, no camp with loot), a bounty
        std::string result;
        bool const triedWanted = roll_chance_f(_wantedChance);
        bool posted = triedWanted && Post(player, PROCEDURALITY_WANTED, false, result);
        if (!posted)
        {
            std::string const wantedResult = result;
            posted = Post(player, PROCEDURALITY_BOUNTY, false, result);
            if (!posted && triedWanted)
                result = Acore::StringFormat("wanted: {} bounty: {}", wantedResult, result);
        }

        LOG_DEBUG("module.procedurality", "Director for {}: {}", player->GetName(), result);
        if (!posted)   // posts already show at level 1
            Debug(2, Acore::StringFormat("nothing for {}: {}", player->GetName(), result));
    }
}

ProceduralityBounty const* ProceduralityMgr::GetBounty(uint32 questId) const
{
    auto itr = _bounties.find(questId);
    return itr != _bounties.end() ? &itr->second : nullptr;
}

std::vector<std::string> ProceduralityMgr::Status() const
{
    std::vector<std::string> lines;
    lines.push_back(Acore::StringFormat("Procedurality director {}: {} / {} bounties live, next quest {}, tick every {} s.",
        _enabled ? "on" : "off", _bounties.size(), _maxActive, _nextQuestId, _intervalMs / IN_MILLISECONDS));

    time_t const now = GameTime::GetGameTime().count();
    std::vector<ProceduralityBounty const*> sorted;
    for (auto const& [questId, bounty] : _bounties)
        sorted.push_back(&bounty);
    std::sort(sorted.begin(), sorted.end(), [](auto const* l, auto const* r) { return l->QuestId < r->QuestId; });

    for (ProceduralityBounty const* bounty : sorted)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(bounty->QuestId);
        CreatureTemplate const* target = sObjectMgr->GetCreatureTemplate(bounty->TargetEntry);
        CreatureTemplate const* giver = sObjectMgr->GetCreatureTemplate(bounty->GiverEntry);
        std::string const what = bounty->Kind == PROCEDURALITY_WANTED
            ? Acore::StringFormat("rare {} ({}, {})", bounty->RareName, bounty->RareEntry,
                bounty->RareGuid ? "up" : bounty->RareDiedAt ? "dead" : "not spawned")
            : Acore::StringFormat("{} x{}", target ? target->Name : "?", bounty->KillCount);
        lines.push_back(Acore::StringFormat("{} \"{}\" ({}): {} lvl {} at {:.0f} {:.0f} r{:.0f}, {} {}, retire in {} min.",
            bounty->QuestId, quest ? quest->GetTitle() : "?", AreaName(bounty->ZoneId), what,
            bounty->Level, bounty->X, bounty->Y, bounty->Radius, giver ? giver->Name : "?",
            bounty->Offered ? Acore::StringFormat("offering for {} min", (bounty->OfferUntil - now) / MINUTE) : "turn-in only",
            (bounty->RetireAt - now) / MINUTE));
    }
    return lines;
}
