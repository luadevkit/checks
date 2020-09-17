#include <lauxlib.h>
#include <string.h>

static int findfield(lua_State *L, int objidx, int level)
{
    if (level == 0 || !lua_istable(L, -1)) return 0;
    lua_pushnil(L);         // nil
    while (lua_next(L, -2)) // k, v
    {
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            if (lua_rawequal(L, objidx, -1))
            {
                lua_pop(L, 1); // k
                return 1;
            }

            if (findfield(L, objidx, level - 1)) // k v k
            {
                lua_pushliteral(L, "."); // k v k "."
                lua_replace(L, -3);      // k "." k
                lua_concat(L, 3);        // "k.k"
                return 1;
            }
        }
        lua_pop(L, 1); // k
    }
    return 0;
}

static int pushglobalfuncname(lua_State *L, lua_Debug *ar)
{
    int top = lua_gettop(L);

    lua_getinfo(L, "f", ar);                              // f
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE); // f LUA_LOADED_TABLE

    if (findfield(L, top + 1, 2)) // f LUA_LOADED_TABLE name
    {
        const char *name = lua_tostring(L, -1); // f LUA_LOADED_TABLE name
        if (strncmp(name, "_G.", 3) == 0)
        {
            lua_pushstring(L, name + 3); // f LUA_LOADED_TABLE name name + 3
            lua_remove(L, -2);           // f LUA_LOADED_TABLE name + 3
        }
        lua_copy(L, -1, top + 1); // name + 3 f LUA_LOADED_TABLE
        lua_settop(L, top + 1);   // name + 3
        return 1;
    }

    lua_settop(L, top);
    return 0;
}

int lerror_error(lua_State *L, int lvl, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    luaL_where(L, lvl + 1);
    lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    lua_concat(L, 2);
    return lua_error(L);
}

int lerror_argerror(lua_State *L, int lvl, int arg, const char *extramsg)
{
    lua_Debug ar;
    if (!lua_getstack(L, lvl, &ar))
    {
        return lerror_error(L, lvl, "bad argument #%d (%s)", arg, extramsg);
    }
    lua_getinfo(L, "n", &ar);
    if (strcmp(ar.namewhat, "method") == 0 && --arg == 0)
    {
        return extramsg ? lerror_error(L, lvl, "calling '%s' on bad self (%s)", ar.name, extramsg)
                        : lerror_error(L, lvl, "calling '%s' on bad self", ar.name);
    }
    if (ar.name == NULL)
    {
        ar.name = pushglobalfuncname(L, &ar) ? lua_tostring(L, -1) : "?";
    }
    return extramsg ? lerror_error(L, lvl, "bad argument #%d to '%s' (%s)", arg, ar.name, extramsg)
                    : lerror_error(L, lvl, "bad argument #%d to '%s'", arg, ar.name);
}
