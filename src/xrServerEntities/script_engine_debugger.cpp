#include "stdafx.h"
#include <ai_space.h>
#include "pch_script.h"
#include "script_engine.h"
#include "script_engine_debugger.h"

static bool isEnabled = true;
bool ShouldAttachDebugger()
{
	return isEnabled;
}

void AttachDebugger()
{
	const char* S = "debugger_attach()";
	shared_str m_script_name = "console_command";
	CScriptEngine se = ai().script_engine();
	lua_State* L = se.lua();
	int l_iErrorCode = luaL_loadbuffer(L, S, xr_strlen(S), "@console_command");

	if (!l_iErrorCode)
	{
		l_iErrorCode = lua_pcall(L, 0, 0, 0);
		if (l_iErrorCode)
		{
			se.print_output(L, *m_script_name, l_iErrorCode);
			return;
		}
	}

	se.print_output(L, *m_script_name, l_iErrorCode);
	isEnabled = true;
}
