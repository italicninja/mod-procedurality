/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 * Formerly core patch 0004-procgen (wowproject).
 */

#ifndef ACORE_PROCEDURALITY_MGR_H
#define ACORE_PROCEDURALITY_MGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class Player;
class Quest;

// Procedural content director (wowproject docs/procedurality.md). Watches where mobs die and where real players
// are, and posts incidents on a quest giver in the same zone: bounties (generated "kill N of X near Y"
// quests) and Wanted rares (a named rare from a reserved creature_template row, plus a quest to kill it).
// Generated quest IDs and rare entries are never reused, and their Quest objects are never freed.
//
// Threading: everything runs on the world thread while no map updates, except OnCreatureDied and
// PassesKillCredit, which map threads call. Those only touch per-continent activity grids (each written by
// its own map's thread) and read the bounty registry, which only changes on the world thread.

enum ProceduralityKind : uint8
{
    PROCEDURALITY_BOUNTY = 0,
    PROCEDURALITY_WANTED = 1,
};

struct ProceduralityBounty
{
    uint8 Kind = PROCEDURALITY_BOUNTY;
    uint32 QuestId = 0;
    uint32 MapId = 0;
    uint32 ZoneId = 0;
    uint32 AreaId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float Radius = 0.0f;
    uint32 TargetEntry = 0;
    uint32 KillCount = 0;
    uint8 Level = 0;
    ObjectGuid::LowType GiverSpawnId = 0;
    uint32 GiverEntry = 0;
    ObjectGuid::LowType ForPlayer = 0; // guid counter of the player it was posted for (0: none)
    time_t Created = 0;
    time_t OfferUntil = 0;             // starter relation removed after this
    time_t RetireAt = 0;               // removed from every quest log after this
    bool Offered = false;

    // Wanted: the rare (X/Y/Z is its spawn point)
    uint32 RareEntry = 0;
    std::string RareName;
    uint8 RareLevel = 0;
    ObjectGuid RareGuid;               // runtime: the summoned rare, if any
    time_t RareDiedAt = 0;             // runtime: when it died or vanished (respawns after Procedurality.Wanted.RespawnMinutes)
    std::vector<uint32> Previews;      // runtime: reward preview entries (ForgeMgr::CreatePreview), freed at retire
};

class ProceduralityMgr
{
public:
    static ProceduralityMgr* instance();

    // WorldScript::OnBeforeWorldInitialized: after quests, quest POIs and quest relations, before the world opens.
    void Load();
    // WorldScript::OnUpdate. Runs the director every Procedurality.Interval.
    void Update(uint32 diff);

    // Map threads.
    void OnCreatureDied(Creature const* creature);
    [[nodiscard]] bool PassesKillCredit(Player const* player, Quest const* quest, uint32 killedEntry, ObjectGuid killed) const;
    // PlayerScript::OnPlayerBeforeQuestRewardItem: a Wanted quest offers a choice of base items; with affixed
    // rewards, each player who turns it in gets a freshly generated (affixed) version of their pick. Returns the item.
    uint32 RollRewardItem(Player* player, Quest const* quest, uint32 baseItem) const;
    // PlayerScript::OnPlayerBeforeSendQuest: send the reward previews' templates first, as their entries may be reused.
    void PushRewardTemplates(Player* player, Quest const* quest) const;

    [[nodiscard]] static bool IsGeneratedQuest(uint32 questId) { return questId >= FIRST_QUEST_ID && questId <= LAST_QUEST_ID; }
    [[nodiscard]] static bool IsRareEntry(uint32 entry) { return entry >= FIRST_RARE_ENTRY && entry <= LAST_RARE_ENTRY; }
    [[nodiscard]] bool IsEnabled() const { return _enabled; }
    [[nodiscard]] bool HasGeneratedQuests() const { return !_bounties.empty(); }

    // Commands (world thread).
    void Tick();                                          // run the director now
    // Post an incident for player. force ignores budgets and cooldowns. result says what happened.
    bool Post(Player* player, ProceduralityKind kind, bool force, std::string& result);
    bool Retire(uint32 questId);
    void ReapplyQuestData();                              // WorldScript::OnAfterQuestDataReload
    std::vector<std::string> Status() const;
    ProceduralityBounty const* GetBounty(uint32 questId) const;

    static constexpr uint32 FIRST_QUEST_ID = 910000;     // 900000-909999: hand-made experiments
    static constexpr uint32 LAST_QUEST_ID = 999999;
    static constexpr uint32 FIRST_RARE_ENTRY = 190100;   // creature_template rows reserved by SQL
    static constexpr uint32 LAST_RARE_ENTRY = 191099;

private:
    ProceduralityMgr() = default;

    struct ActivityCell
    {
        float Kills = 0.0f;    // decayed count
        time_t Stamp = 0;
    };
    using ActivityGrid = std::unordered_map<uint32, ActivityCell>;

    struct Text
    {
        uint32 TypeMask;
        uint32 Weight;
        std::string Value;
    };

    void LoadConfig();
    void LoadTexts();
    void LoadWorldMapAreas();
    void LoadBounties();
    void LoadRareBases();

    float ActivityAround(uint32 mapId, float x, float y, float radius) const;
    std::string PickText(std::string const& kind, uint32 creatureType, std::unordered_map<std::string, std::string> const& vars) const;

    Quest* BuildQuest(ProceduralityBounty const& bounty, std::unordered_map<std::string, std::string> const& vars,
        uint32 creatureType, uint32 races);
    std::string RareName(uint32 creatureType) const;
    bool FillRareTemplate(uint32 entry, uint32 baseEntry, std::string const& name, uint8 level) const;
    void MaintainRares(time_t now);
    void SpawnRare(ProceduralityBounty& bounty);
    void DespawnRare(ProceduralityBounty& bounty) const;
    Quest* LoadQuest(uint32 questId) const;
    void Register(ProceduralityBounty const& bounty, Quest* quest);
    void SetOffered(ProceduralityBounty& bounty, bool offered);
    void ApplyPOIs(ProceduralityBounty const& bounty) const;
    void RefreshGiverMarkers(ProceduralityBounty const& bounty) const;
    void Announce(ProceduralityBounty const& bounty, std::string const& say) const;
    void RetireRows(uint32 questId) const;
    // Procedurality.Debug: 1 = events, 2 = also every director decision; sent to online GM (and above) accounts
    void Debug(uint8 level, std::string const& text) const;

    bool _enabled = false;
    bool _loaded = false;              // Load found the tables and read them
    uint8 _debug = 0;
    uint32 _intervalMs = 60 * 1000;
    uint32 _timer = 0;
    float _halfLifeSeconds = 30 * 60.0f;
    uint32 _maxActive = 30;
    uint32 _maxPerZone = 2;
    uint32 _maxPerGiver = 3;
    uint32 _playerCooldown = 20 * 60;
    float _nearbyYards = 700.0f;
    float _searchYards = 700.0f;
    float _minYards = 100.0f;
    float _campYards = 80.0f;
    uint32 _minCampSize = 5;
    float _giverYards = 900.0f;
    uint32 _xpDifficulty = 5;
    float _moneyRate = 1.0f;
    uint32 _offerSeconds = 120 * 60;
    uint32 _graceSeconds = 120 * 60;
    float _wantedChance = 35.0f;
    uint32 _wantedMaxActive = 5;
    uint32 _wantedMaxPerZone = 1;
    float _rareHealth = 3.0f;
    float _rareDamage = 1.3f;
    uint32 _rareRespawnSeconds = 10 * 60;
    float _rareBlueChance = 25.0f;
    uint32 _wantedXpDifficulty = 6;
    bool _affixRewards = true;         // Procedurality.Rewards.Affixes and ForgeMgr on: rewards rolled with affixes

    // Continents only; the outer map is fixed after Load, each grid is written by its own map's thread.
    std::unordered_map<uint32, ActivityGrid> _activity;

    std::unordered_map<std::string, std::vector<Text>> _texts;
    std::unordered_map<uint32, uint32> _worldMapAreaByZone;

    std::unordered_map<uint32, ProceduralityBounty> _bounties;               // live (not retired), by quest id
    std::unordered_map<ObjectGuid::LowType, time_t> _lastPostedFor;    // player guid counter -> time
    uint32 _nextQuestId = FIRST_QUEST_ID;
    uint32 _nextRareEntry = FIRST_RARE_ENTRY;
    uint32 PickRewardBase(std::size_t quality, std::size_t category, uint8 itemLevel) const;

    // Affixable base items for Wanted rewards: [green, blue][category][item level]. Categories: cloth, leather,
    // mail, plate, cloak/neck/finger, weapon (one reward choice each). Fixed after Load.
    static constexpr std::size_t REWARD_CATEGORIES = 6;
    std::vector<uint32> _rewardBases[2][REWARD_CATEGORIES][256];
};

#define sProceduralityMgr ProceduralityMgr::instance()

#endif
