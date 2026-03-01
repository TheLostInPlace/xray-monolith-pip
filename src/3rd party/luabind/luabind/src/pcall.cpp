// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.
#include "luabind_api.h"
#include <luabind/detail/pcall.hpp>
#include <luabind/error.hpp>
#include <luabind/lua_include.hpp>
#include <windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

extern void Msg(const char* format, ...);

namespace luabind {
    namespace detail
    {
        // Static initialization flag
        static bool g_symbols_initialized = false;
        static HANDLE g_process = nullptr;

        static void initialize_symbols()
        {
            if (g_symbols_initialized)
                return;

            g_process = GetCurrentProcess();
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            SymInitialize(g_process, nullptr, TRUE);
            g_symbols_initialized = true;
        }

        static void print_cpp_stack_trace()
        {
            initialize_symbols();

            const int MAX_FRAMES = 32;
            void* stack[MAX_FRAMES];

            DWORD frame_count = CaptureStackBackTrace(0, MAX_FRAMES, stack, nullptr);

            Msg("!C++ Call Stack (%lu frames):", frame_count);

            SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            for (DWORD i = 0; i < frame_count; ++i)
            {
                DWORD64 address = (DWORD64)stack[i];

                DWORD displacement = 0;
                DWORD64 displacement64 = 0;

                const char* symbol_name = "???";
                const char* file_name = "???";
                DWORD line_number = 0;

                // Get symbol name
                if (SymFromAddr(g_process, address, &displacement64, symbol))
                {
                    symbol_name = symbol->Name;
                }

                // Get file and line number
                if (SymGetLineFromAddr64(g_process, address, &displacement, &line))
                {
                    file_name = line.FileName;
                    line_number = line.LineNumber;
                }

                // Shorten file path - show last 70 chars
                const char* short_file = file_name;
                size_t path_len = strlen(file_name);
                if (path_len > 70)
                {
                    short_file = file_name + path_len - 70;
                }

                if (line_number > 0)
                {
                    Msg("! [CPP] %2lu : %s(%lu) : %s + 0x%llX", i, short_file, line_number, symbol_name, displacement64);
                }
                else
                {
                    Msg("! [CPP] %2lu : %s : %s + 0x%llX", i, short_file, symbol_name, displacement64);
                }
            }

            free(symbol);
        }

        static void print_lua_stack_trace(lua_State* L)
        {
            Msg("!Lua Call Stack:");

            lua_Debug ar;
            int level = 0;
            int count = 0;

            while (lua_getstack(L, level, &ar) && count < 64)
            {
                if (lua_getinfo(L, "Sln", &ar) == 0)
                {
                    break;
                }

                const char* source = ar.source ? ar.source : "?";
                const char* name = ar.name ? ar.name : "?";

                // Determine if this is a C function or Lua function
                const char* type = (ar.what && ar.what[0] == 'C') ? "[C  ]" : "[Lua]";

                // Shorten source path if it's too long
                const char* short_source = source;
                if (source && source[0] == '@')
                {
                    short_source = source + 1;  // Skip the '@' prefix
                }

                if (ar.currentline > 0)
                {
                    Msg("! [LUA] %2d : %s %s(%d) : %s", level, type, short_source, ar.currentline, name);
                }
                else
                {
                    Msg("! [LUA] %2d : %s %s : %s", level, type, short_source, name);
                }

                level++;
                count++;
            }

            if (count == 0)
            {
                Msg("! [LUA]   0 : No call stack available (exception outside Lua context)");
            }
        }

        static void print_lua_stack(lua_State* L)
        {
            int top = lua_gettop(L);

            if (top == 0)
            {
                Msg("!Lua Value Stack: Empty");
                return;
            }

            Msg("!Lua Value Stack (%d items):", top);

            for (int i = 1; i <= top; ++i)
            {
                int type = lua_type(L, i);
                const char* type_name = lua_typename(L, type);

                switch (type)
                {
                case LUA_TNIL:
                    Msg("![%d] %s: nil", i, type_name);
                    break;
                case LUA_TBOOLEAN:
                    Msg("![%d] %s: %s", i, type_name, lua_toboolean(L, i) ? "true" : "false");
                    break;
                case LUA_TNUMBER:
                    Msg("![%d] %s: %g", i, type_name, lua_tonumber(L, i));
                    break;
                case LUA_TSTRING:
                {
                    const char* str = lua_tostring(L, i);
                    if (str && strlen(str) < 256)
                    {
                        Msg("![%d] %s: \"%s\"", i, type_name, str);
                    }
                    else
                    {
                        Msg("![%d] %s: \"<long string>\"", i, type_name);
                    }
                    break;
                }
                case LUA_TTABLE:
                    Msg("![%d] %s: 0x%p", i, type_name, lua_topointer(L, i));
                    break;
                case LUA_TFUNCTION:
                    Msg("![%d] %s: 0x%p", i, type_name, lua_topointer(L, i));
                    break;
                case LUA_TUSERDATA:
                    Msg("![%d] %s: 0x%p", i, type_name, lua_topointer(L, i));
                    break;
                case LUA_TTHREAD:
                    Msg("![%d] %s: 0x%p", i, type_name, lua_topointer(L, i));
                    break;
                default:
                    Msg("![%d] %s: unknown", i, type_name);
                    break;
                }
            }
        }

        int pcall(lua_State* L, int nargs, int nresults)
        {
            pcall_callback_fun e = get_pcall_callback();
            int en = 0;
            if (e)
            {
                int base = lua_gettop(L) - nargs;
                lua_pushcfunction(L, e);
                lua_insert(L, base);  // push pcall_callback under chunk and args
                en = base;
            }

            int result = 0;

            try
            {
                result = lua_pcall(L, nargs, nresults, en);
            }
            catch (const std::exception& e)
            {
                Msg("!========================================");
                Msg("!CRITICAL: C++ Exception in Lua pcall");
                Msg("!Exception Type: std::exception");
                Msg("!Exception: %s", e.what());
                Msg("!========================================");

                print_cpp_stack_trace();
                print_lua_stack_trace(L);
                print_lua_stack(L);

                Msg("!Game state may be corrupted. Forcing shutdown.");
                Msg("!========================================");

                // Clean up Lua stack
                if (en)
                    lua_remove(L, en);

                return LUA_ERRRUN;
            }
            catch (const char* s)
            {
                Msg("!========================================");
                Msg("!CRITICAL: C++ Exception in Lua pcall");
                Msg("!Exception Type: const char*");
                Msg("!Exception: %s", s);
                Msg("!========================================");

                print_cpp_stack_trace();
                print_lua_stack_trace(L);
                print_lua_stack(L);

                Msg("!Game state may be corrupted. Forcing shutdown.");
                Msg("!========================================");

                if (en)
                    lua_remove(L, en);

                return LUA_ERRRUN;
            }
            catch (...)
            {
                Msg("!========================================");
                Msg("!CRITICAL: Unknown C++ Exception in Lua pcall");
                Msg("!Exception Type: Structured Exception (SEH)");
                Msg("!========================================");

                print_cpp_stack_trace();
                print_lua_stack_trace(L);
                print_lua_stack(L);

                Msg("!Game state may be corrupted. Forcing shutdown.");
                Msg("!========================================");

                if (en)
                    lua_remove(L, en);

                return LUA_ERRRUN;
            }

            if (en)
                lua_remove(L, en);  // remove pcall_callback

            return result;
        }

        int resume_impl(lua_State* L, int nargs, int)
        {
            return lua_resume(L, nargs);
        }

    }
}
luabind::memory_allocation_function_pointer		luabind::allocator = 0;
luabind::memory_allocation_function_parameter	luabind::allocator_parameter = 0;
