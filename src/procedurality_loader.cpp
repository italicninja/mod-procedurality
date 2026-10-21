/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 */

#include "Chat.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldSession.h"

void AddProceduralityScripts();
void AddSC_procedurality_commandscript();

// The version this source is tagged with in https://github.com/italicninja/mod-procedurality (bump it with each tag).
constexpr char const* PROCEDURALITY_VERSION = "v0.1.0";

namespace
{
    // Tells each real player at login that the realm runs this module, like mod-playerbots does. GPL-2.0: the link
    // must lead to the module's public source.
    class ProceduralityLoginScript : public PlayerScript
    {
    public:
        ProceduralityLoginScript() : PlayerScript("ProceduralityLoginScript", {PLAYERHOOK_ON_LOGIN}) { }

        void OnPlayerLogin(Player* player) override
        {
            if (player->GetSession()->IsHeadless())
                return;
            std::string line = "|cff00ff00This server runs with |cff00ccffmod-procedurality|r |cffffd100";
            line += PROCEDURALITY_VERSION;
            line += "|r |cffcccccchttps://github.com/italicninja/mod-procedurality|r";
            ChatHandler(player->GetSession()).SendSysMessage(line);
        }
    };
}

// Called by the core's module loader (Add<module name with '-' as '_'>Scripts).
void Addmod_proceduralityScripts()
{
    AddProceduralityScripts();
    AddSC_procedurality_commandscript();
    new ProceduralityLoginScript();
}
