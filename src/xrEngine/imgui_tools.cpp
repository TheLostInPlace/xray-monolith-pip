#include "stdafx.h"
#include <imgui.h>
#include "../xrServerEntities/script_engine_debugger.h"

void InitImguiTools()
{
    auto& States = Engine.External.EditorStates;
    bool* luaDebug = &States[static_cast<u8>(EditorUI::LuaDebug)];
    if (ImGui::MenuItem("Lua: Attach to VSCode (Doesn't work)", nullptr, luaDebug))
    {
        if (*luaDebug)
        {
            //AttachDebugger(); //Getting crash in ljtab.c after executing this function: Unhandled exception thrown: read access violation.
        }
    }
}