/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 * Formerly core patch 0004-procgen (wowproject).
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Player.h"
#include "ProceduralityMgr.h"
#include "RBAC.h"

using namespace Acore::ChatCommands;

// GM tools for the procedural content director (wowproject docs/procedurality.md).
class procedurality_commandscript : public CommandScript
{
public:
    procedurality_commandscript() : CommandScript("procedurality_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable proceduralityCommandTable =
        {
            { "status", HandleProceduralityStatusCommand, rbac::RBAC_PERM_COMMAND_ADDITEM, Console::Yes },
            { "bounty", HandleProceduralityBountyCommand, rbac::RBAC_PERM_COMMAND_ADDITEM, Console::No },
            { "wanted", HandleProceduralityWantedCommand, rbac::RBAC_PERM_COMMAND_ADDITEM, Console::No },
            { "retire", HandleProceduralityRetireCommand, rbac::RBAC_PERM_COMMAND_ADDITEM, Console::Yes },
            { "tick",   HandleProceduralityTickCommand,   rbac::RBAC_PERM_COMMAND_ADDITEM, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "procedurality", proceduralityCommandTable },
            { "proc",          proceduralityCommandTable },
        };
        return commandTable;
    }

    // .procedurality status: director state and live bounties
    static bool HandleProceduralityStatusCommand(ChatHandler* handler)
    {
        for (std::string const& line : sProceduralityMgr->Status())
            handler->SendSysMessage(line);
        return true;
    }

    // .procedurality bounty / wanted: post one for yourself now, ignoring budgets and cooldowns
    static bool HandleProceduralityBountyCommand(ChatHandler* handler)
    {
        return Post(handler, PROCEDURALITY_BOUNTY);
    }

    static bool HandleProceduralityWantedCommand(ChatHandler* handler)
    {
        return Post(handler, PROCEDURALITY_WANTED);
    }

    static bool Post(ChatHandler* handler, ProceduralityKind kind)
    {
        std::string result;
        bool const posted = sProceduralityMgr->Post(handler->GetSession()->GetPlayer(), kind, true, result);
        if (!posted)
        {
            handler->SendErrorMessage(result, false);
            return false;
        }

        handler->SendSysMessage(result);
        return true;
    }

    // .procedurality retire <questId>: take a bounty out of every quest log and off its giver
    static bool HandleProceduralityRetireCommand(ChatHandler* handler, uint32 questId)
    {
        if (!sProceduralityMgr->Retire(questId))
        {
            handler->SendErrorMessage("No live bounty with quest ID {}.", questId);
            return false;
        }

        handler->PSendSysMessage("Retired bounty {}.", questId);
        return true;
    }

    // .procedurality tick: run the director now (expiry, then a bounty for each real player within budget)
    static bool HandleProceduralityTickCommand(ChatHandler* handler)
    {
        sProceduralityMgr->Tick();
        handler->SendSysMessage("Director ran.");
        return true;
    }
};

void AddSC_procedurality_commandscript()
{
    new procedurality_commandscript();
}
