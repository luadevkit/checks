/***
 * Function argument checking.
 * @module ldk.checks
 */

#include "lerror.h"

#include <assert.h>
#include <ctype.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHK_EUSERDATA (1 << 0)
#define CHK_EFILE (1 << 1)

#define CHK_STRLEN(x) (sizeof(x) - 1)
#define CHK_STRNEQ(x, xl, y, yl) ((yl) == (xl) && strncmp(x, y, xl) == 0)

static const char *getmetafield(lua_State *L, int arg, const char *field)
{
    const char *value = NULL;
    switch (luaL_getmetafield(L, arg, field))
    {
        case LUA_TNIL:
            break;
        case LUA_TSTRING:
            value = lua_tostring(L, -1);
        default:
            lua_pop(L, 1);
    }
    return value;
}

static const char *typename(lua_State *L, int arg, int *rawtype)
{
    *rawtype = lua_type(L, arg);
    if (*rawtype == LUA_TNUMBER)
    {
        return lua_isinteger(L, arg) ? "integer" : "float";
    }

    const char *name = NULL;
    if (*rawtype == LUA_TUSERDATA)
    {
        name = getmetafield(L, arg, "__name");
    }
    else if (*rawtype == LUA_TTABLE)
    {
        name = getmetafield(L, arg, "__type");
    }
    return name ? name : lua_typename(L, *rawtype);
}

static inline void append_one(luaL_Buffer *b, const char *p, size_t n, bool is_option)
{
    if (is_option)
    {
        luaL_addchar(b, '\'');
        luaL_addlstring(b, p, n);
        luaL_addchar(b, '\'');
    }
    else if (CHK_STRNEQ("any", 3, p, n))
    {
        luaL_addstring(b, "not nil");
    }
    else
    {
        luaL_addlstring(b, p, n);
    }
}

static void append_descriptor(luaL_Buffer *b, const char *descriptor, const char *descriptor_end,
                              bool is_option)
{
    int n = 0;

    const char *p = descriptor;
    if (*p == '?')
    {
        luaL_addstring(b, "nil");
        p++;
        n++;
    }
    for (const char *q = strchr(p, '|'); q != NULL; p = q + 1, q = strchr(p, '|'), n++)
    {
        if (p != descriptor) luaL_addstring(b, ", ");
        append_one(b, p, (size_t)(q - p), is_option);
    }
    if (p != descriptor) luaL_addstring(b, n > 2 ? ", or " : " or ");
    append_one(b, p, (size_t)(descriptor_end - p), is_option);
}

static int type_error(lua_State *L, int arg, int type, const char *got, const char *expected,
                      const char *expected_end, int flags)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    append_descriptor(&b, expected, expected_end, false);
    luaL_addstring(&b, " expected, got ");
    if (flags & CHK_EFILE)
    {
        luaL_addstring(&b, got);
    }
    else if ((flags & CHK_EUSERDATA) && type == LUA_TUSERDATA)
    {
        luaL_addstring(&b, "userdata");
    }
    else
    {
        luaL_addstring(&b, lua_typename(L, type));
    }
    luaL_pushresult(&b);
    return lerror_argerror(L, 1, arg, lua_tostring(L, -1));
}

static bool type_match_one(int rawtype, const char *got, size_t got_len, const char *expected,
                           size_t expected_len, int *flags)
{
    if (CHK_STRNEQ(got, got_len, expected, expected_len)) return true;
    if (rawtype == LUA_TNUMBER)
    {
        return CHK_STRNEQ("number", 6, expected, expected_len);
    }
    if (rawtype == LUA_TUSERDATA)
    {
        if (CHK_STRNEQ("file", 4, expected, expected_len))
        {
            *flags |= CHK_EFILE;
            return CHK_STRNEQ(LUA_FILEHANDLE, CHK_STRLEN(LUA_FILEHANDLE), got, got_len);
        }
        if (CHK_STRNEQ("userdata", 8, expected, expected_len))
        {
            *flags |= CHK_EUSERDATA;
            return true;
        }
        return false;
    }
    return rawtype != LUA_TNONE && rawtype != LUA_TNIL
           && CHK_STRNEQ("any", 3, expected, expected_len);
}

static bool type_match(int rawtype, const char *got, const char *expected, const char *expected_end,
                       int *flags)
{
    size_t got_len = strlen(got);

    *flags = 0;
    const char *p = expected;
    for (const char *q = strchr(p, '|'); q != NULL; p = q + 1, q = strchr(p, '|'))
    {
        if (type_match_one(rawtype, got, got_len, p, (size_t)(q - p), flags)) return true;
    }
    return type_match_one(rawtype, got, got_len, p, (size_t)(expected_end - p), flags);
}

static int check_one(lua_State *L, int arg, const char *expected, size_t expected_len)
{
    if (expected_len == 0) return 0;

    int rawtype;
    const char *got = typename(L, -1, &rawtype);

    const char *p = expected;
    const char *e = expected + expected_len;

    if (*p == '?')
    {
        if (rawtype == LUA_TNIL)
        {
            lua_pop(L, 1);
            return 0;
        }
        p++;
    }

    int flags;
    if (type_match(rawtype, got, p, e, &flags)) return 0;
    return type_error(L, arg, rawtype, got, expected, e, flags);
}

/***
 * Checks whether the specified argument of the calling function is of any of the types specified in
 * the given descriptor.
 *
 * The type of the argument at position `arg` must match one of the types in `expected.
 *
 * Each type can be the name of a primitive Lua type:
 *
 * * `nil`
 * * `boolean`
 * * `userdata`
 * * `number`
 * * `string`
 * * `table`
 * * `function`
 * * `thread`
 *
 * one of the following options:
 *
 * * `any` (accepts any non-nil argument type)
 * * `file` (accepts a file object)
 * * `integer` (accepts an integer number)
 * * `float` (accepts a floating point number)
 * * an arbitrary string, matched against the content of the `__type` or `__name` field of the
 * argument's metatable if the argument is table or a userdata, respectively.
 *
 * Multiple values can be accepted if concatenated with a bar `'|'`:
 *
 *    check_type(1, 'integer|table') -- matches an integer or a table
 *
 * Finally, a `nil` value can be matched by prefixing the type descriptor with a question mark:
 *
 *    checktype(1, '?table') -- matches a table or nil
 *    checktype(1, 'nil|table') -- equivalent to the above
 *
 * Finally, prefixing a type with `'*'` will
 * @function check_type
 * @tparam integer arg position of the argument to be tested.
 * @tparam string expected the descriptor of the expected type.
 * @usage
 *    local function foo(t, filter)
 *      check_type(1, 'table')
 *      check_type(2, '?function')
 *      ...
 */
static int checks_check_type(lua_State *L)
{
    lua_Debug ar;
    lua_getstack(L, 1, &ar);

    int arg = (int)luaL_checkinteger(L, 1);
    size_t expected_len;
    const char *expected = luaL_checklstring(L, 2, &expected_len);

    if (!lua_getlocal(L, &ar, arg))
    {
        return luaL_argerror(L, 1, "invalid argument index");
    }
    return check_one(L, arg, expected, expected_len);
}

/***
 * Checks whether the arguments of the calling function are of the specified types.
 *
 * If last descriptor is prefixed with a `'*'`, it will match any remaining argument
 * passed to the function.
 *
 * @function check_types
 * @param ... the descriptors of the expected types (see @{check_type}).
 * @usage
 *    local function foo(t, filter)
 *      check_types('table', '?function')
 *      ...
 */
static int checks_check_types(lua_State *L)
{
    lua_Debug ar;
    lua_getstack(L, 1, &ar);

    bool eat_all = false;
    size_t expected_len;
    const char *expected;

    int n = lua_gettop(L);
    for (int arg = 1; arg <= n; arg++)
    {
        expected = luaL_checklstring(L, arg, &expected_len);
        if (expected_len > 0 && *expected == '*')
        {
            if (arg < n)
            {
                return luaL_argerror(L, arg, "only the last descriptor can be prefixed by '*'");
            }
            eat_all = true;
            expected++;
            expected_len--;
        }

        if (!lua_getlocal(L, &ar, arg) && !eat_all)
        {
            return luaL_argerror(L, arg, "no more arguments");
        }
        if (check_one(L, arg, expected, expected_len) < 0)
        {
            return luaL_argerror(L, arg, "bad descriptor");
        }
    }

    while(eat_all && lua_getlocal(L, &ar, ++n))
    {
        check_one(L, n, expected, expected_len);
    }

    return 0;
}

/**
 * Raises an error reporting a problem with the argument of the calling function at the specified
 * position.
 *
 * This function never returns.
 *
 * @function arg_error
 * @tparam integer arg the argument position.
 * @tparam[opt] string message additional text to use as comment.
 * @usage
 *    local function foo(x)
 *      arg_error(1, "message...")
 *      ...
 */
static int checks_arg_error(lua_State *L)
{
    int arg = (int)luaL_checkinteger(L, 1);
    return lerror_argerror(L, 1, arg, luaL_optstring(L, 2, NULL));
}

/**
 * Raises an error reporting a problem with the argument of the calling function at the specified
 * position if the given condition is not satisfied.
 *
 * @function check_arg
 * @tparam integer arg the position of the argument to be tested.
 * @tparam bool condition condition to check.
 * @tparam[opt] string message additional text to use as comment.
 * @usage
 *    local function foo(t, filter)
 *      check_type(1, 'table')
 *      check_arg(1, #t > 0, "empty table")
 *      ...
 */
static int checks_check_arg(lua_State *L)
{
    int arg = (int)luaL_checkinteger(L, 1);
    if (lua_toboolean(L, 2)) return 0;
    return lerror_argerror(L, 1, arg, luaL_optstring(L, 3, NULL));
}

static int option_error(lua_State *L, int arg, const char *got, const char *expected,
                        const char *expected_end)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    append_descriptor(&b, expected, expected_end, true);
    luaL_addstring(&b, " expected, got '");
    luaL_addstring(&b, got);
    luaL_addchar(&b, '\'');
    luaL_pushresult(&b);
    return lerror_argerror(L, 1, arg, lua_tostring(L, -1));
}

static bool options_match(const char *got, const char *expected, const char *expected_end)
{
    size_t got_len = strlen(got);

    const char *p = expected;
    for (const char *q = strchr(p, '|'); q != NULL; p = q + 1, q = strchr(p, '|'))
    {
        if (CHK_STRNEQ(got, got_len, p, (size_t)(q - p))) return true;
    }
    return CHK_STRNEQ(got, got_len, p, (size_t)(expected_end - p));
}

/**
 * Checks whether an argument of the calling function is a string and its value is
 * any of the values specified in a given descriptor.
 *
 * Multiple values can be accepted if concatenated with a bar `'|'`:
 *
 *    check_option(1, 'one|two') -- accepts both `'one'` and `'two'`.
 *
 * A `nil` value can be accepted by prefixing the value descriptor with a
 * question mark:
 *
 *    check_option(1, '?one') -- matches 'one' or nil
 *
 * @function check_option
 * @tparam integer arg the position of the argument to be tested.
 * @tparam string expected the acceptable values descriptor.
 * @usage
 *    local function foo(mode)
 *      check_option(1, 'read|write')
 *      ...
 * @raise if the descriptor is `nil` or empty; or if the argument position is invalid.
 */
static int checks_check_option(lua_State *L)
{
    lua_Debug ar;
    lua_getstack(L, 1, &ar);

    int arg = (int)luaL_checkinteger(L, 1);
    size_t options_len;
    const char *options = luaL_checklstring(L, 2, &options_len);
    if (options_len == 0)
    {
        return luaL_argerror(L, 2, "empty descriptor");
    }

    if (!lua_getlocal(L, &ar, arg))
    {
        return luaL_argerror(L, 1, "invalid argument index");
    }

    int rawtype;
    const char *got = typename(L, -1, &rawtype);

    const char *p = options;
    const char *e = options + options_len;
    if (*p == '?')
    {
        if (rawtype == LUA_TNIL)
        {
            lua_pop(L, 1);
            return 0;
        }
        p++;
    }

    if (rawtype != LUA_TSTRING)
    {
        char *expected = "string";
        return type_error(L, arg, rawtype, got, expected, expected + 6, 0);
    }

    got = lua_tostring(L, -1);
    if (options_match(got, p, e)) return 0;
    return option_error(L, arg, got, options, e);
}

// clang-format off
static const struct luaL_Reg funcs[] =
{
#define XX(name) { #name, checks_ ##name },
    XX(arg_error)
    XX(check_arg)
    XX(check_option)
    XX(check_type)
    XX(check_types)
    { NULL, NULL }
#undef XX
};
//clang-format on

extern int luaopen_ldk_checks(lua_State *L)
{
  luaL_newlib(L, funcs);
  return 1;
}
