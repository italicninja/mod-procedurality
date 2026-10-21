/*
 * This file is part of the mod-procedurality module for AzerothCore, released under GNU GPL v2 license,
 * redistribute/modify under version 2 of the License, or (at your option) any later version.
 */

void AddProceduralityScripts();
void AddSC_procedurality_commandscript();

// Called by the core's module loader (Add<module name with '-' as '_'>Scripts).
void Addmod_proceduralityScripts()
{
    AddProceduralityScripts();
    AddSC_procedurality_commandscript();
}
