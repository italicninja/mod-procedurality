/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 */

#include "Creature.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ProceduralityMgr.h"
#include "QuestDef.h"
#include "UnitScript.h"
#include "WorldScript.h"

// Hooks into the core (generic ones from core patch 0004-generated-quest-hooks); the logic is ProceduralityMgr's.
namespace
{
    class ProceduralityWorldScript : public WorldScript
    {
    public:
        ProceduralityWorldScript()
            : WorldScript("ProceduralityWorldScript", {WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED, WORLDHOOK_ON_UPDATE,
                WORLDHOOK_ON_AFTER_QUEST_DATA_RELOAD})
        {
        }

        // After quests, quest POIs and quest relations, before the world opens (nothing has loaded a character)
        void OnBeforeWorldInitialized() override { sProceduralityMgr->Load(); }

        // World thread, after the (synchronous) map update
        void OnUpdate(uint32 diff) override { sProceduralityMgr->Update(diff); }

        void OnAfterQuestDataReload() override { sProceduralityMgr->ReapplyQuestData(); }
    };

    class ProceduralityPlayerScript : public PlayerScript
    {
    public:
        ProceduralityPlayerScript()
            : PlayerScript("ProceduralityPlayerScript", {PLAYERHOOK_PASSED_QUEST_KILLED_MONSTER_CREDIT,
                PLAYERHOOK_ON_BEFORE_QUEST_REWARD_ITEM, PLAYERHOOK_ON_BEFORE_SEND_QUEST})
        {
        }

        // Map threads: bounties only count their target's kills inside their area
        bool OnPlayerPassedQuestKilledMonsterCredit(Player* player, Quest const* quest, uint32 /*entry*/, uint32 realEntry, ObjectGuid guid) override
        {
            return !ProceduralityMgr::IsGeneratedQuest(quest->GetQuestId()) || sProceduralityMgr->PassesKillCredit(player, quest, realEntry, guid);
        }

        // Wanted rewards: each player who turns one in gets a freshly rolled (affixed) copy of their pick
        void OnPlayerBeforeQuestRewardItem(Player* player, Quest const* quest, uint32& itemId) override
        {
            if (ProceduralityMgr::IsGeneratedQuest(quest->GetQuestId()))
                itemId = sProceduralityMgr->RollRewardItem(player, quest, itemId);
        }

        // Reward previews' templates before the dialog, so a reused entry's tooltip isn't stale
        void OnPlayerBeforeSendQuest(Player* player, Quest const* quest) override
        {
            if (ProceduralityMgr::IsGeneratedQuest(quest->GetQuestId()))
                sProceduralityMgr->PushRewardTemplates(player, quest);
        }
    };

    class ProceduralityUnitScript : public UnitScript
    {
    public:
        ProceduralityUnitScript() : UnitScript("ProceduralityUnitScript", true, {UNITHOOK_ON_UNIT_DEATH}) { }

        // Map threads: the activity map counts open-world kills
        void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
        {
            if (Creature* creature = unit->ToCreature())
                sProceduralityMgr->OnCreatureDied(creature);
        }
    };
}

void AddProceduralityScripts()
{
    new ProceduralityWorldScript();
    new ProceduralityPlayerScript();
    new ProceduralityUnitScript();
}
