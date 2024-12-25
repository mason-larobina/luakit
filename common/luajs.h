/*
 * Copyright © 2016 Aidan Holm <aidanholm@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef LUAKIT_COMMON_LUAJS_H
#define LUAKIT_COMMON_LUAJS_H

#include <jsc/jsc.h>
#include <stdbool.h>
#include <glib.h>
#include <lua.h>

int luajs_eval_js(lua_State *L, JSCContext *ctx, const char *code, const char *source, guint line, bool no_return);
int luajs_pushvalue(lua_State *L, JSCValue *value);
JSCValue *luajs_tovalue(lua_State *L, int idx, JSCContext *ctx);

#endif /* end of include guard: LUAKIT_COMMON_LUAJS_H */

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
