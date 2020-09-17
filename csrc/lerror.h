#include <lua.h>

int lerror_error(lua_State *L, int lvl, const char *fmt, ...);
int lerror_argerror(lua_State *L, int lvl, int arg, const char *extramsg);
