/*
 * luaclass.c - useful functions for handling Lua classes
 *
 * Copyright © 2010 Mason Larobina <mason.larobina@gmail.com>
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#include "common/luaclass.h"
#include "common/luaobject.h"

#include <stdlib.h>

struct lua_class_property {
    /** Callback function called when the property is found in object creation. */
    lua_class_propfunc_t new;
    /** Callback function called when the property is found in object __index. */
    lua_class_propfunc_t index;
    /** Callback function called when the property is found in object __newindex. */
    lua_class_propfunc_t newindex;
};

static GPtrArray *luaH_classes = NULL;

/** Convert an object to a userdata if possible.
 *
 * \param L The Lua VM state.
 * \param ud The index of the object to convert.
 * \param class The to try and convert it to.
 * \return A pointer to the converted object, NULL otherwise.
 */
gpointer
luaH_toudata(lua_State *L, gint ud, lua_class_t *class) {
    gpointer p = lua_touserdata(L, ud);
    if(p) /* value is a userdata? */
        if(lua_getmetatable(L, ud)) /* does it have a metatable? */
        {
            lua_pushlightuserdata(L, class);
            lua_rawget(L, LUA_REGISTRYINDEX);
            if(!lua_rawequal(L, -1, -2)) /* does it have the correct mt? */
                p = NULL;
            lua_pop(L, 2); /* remove both metatables */
        }
    return p;
}

/** Checks if the object at the given index is a userdata of the given class.
 * Raises a Lua type error if it is not.
 *
 * \param L The Lua VM state.
 * \param ud The object index on the stack.
 * \param class The wanted class.
 * \return A pointer to the converted object.
 */
gpointer
luaH_checkudata(lua_State *L, gint ud, lua_class_t *class) {
    gpointer p = luaH_toudata(L, ud, class);
    if(!p)
        luaL_typerror(L, ud, class->name);
    return p;
}

/** Get an object's \ref lua_class_t.
 *
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 * \return The \ref lua_class_t of the object, if it has one. \c NULL otherwise.
 */
lua_class_t *
luaH_class_get(lua_State *L, gint idx) {
    gint type = lua_type(L, idx);
    lua_class_t *class;

    if(type == LUA_TUSERDATA && luaH_classes)
        for (guint i = 0; i < luaH_classes->len; i++) {
            class = luaH_classes->pdata[i];
            if(luaH_toudata(L, idx, class))
                return class;
        }

    return NULL;
}

/** Enhanced version of lua_typename that recognizes setup Lua classes.
 *
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
const gchar *
luaH_typename(lua_State *L, gint idx) {
    gint type = lua_type(L, idx);

    if(type == LUA_TUSERDATA) {
        lua_class_t *lua_class = luaH_class_get(L, idx);
        if(lua_class)
            return lua_class->name;
    }
    return lua_typename(L, type);
}

/** Registers a library under a global name in Lua.
 *
 * \param L The Lua VM state.
 * \param name The name under which the library should be accessible in Lua.
 * \param methods The methods of the library.
 * \param meta The methods of the library's metatable.
 */
void
luaH_openlib(lua_State *L, const gchar *name, const struct luaL_reg methods[],
        const struct luaL_reg meta[]) {
    luaL_newmetatable(L, name);                                        /* 1 */
    lua_pushvalue(L, -1);           /* dup metatable                      2 */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable      1 */

    luaL_register(L, NULL, meta);                                      /* 1 */
    luaL_register(L, name, methods);                                   /* 2 */
    lua_pushvalue(L, -1);           /* dup self as metatable              3 */
    lua_setmetatable(L, -2);        /* set self as metatable              2 */
    lua_pop(L, 2);
}

/** Adds a property to a \ref lua_class_t.
 * All callbacks functions can also be NULL, in which case the property either
 * cannot be initialized, set or read.
 *
 * \param lua_class The class to add the property to.
 * \param token The property's name.
 * \param cb_new The function to call when the property is set on
 *          initialization.
 * \param cb_index The function to call when the property is read.
 * \param cb_newindex The function to call when the property is set.
 */
void
luaH_class_add_property(lua_class_t *lua_class, luakit_token_t token,
        lua_class_propfunc_t cb_new,
        lua_class_propfunc_t cb_index,
        lua_class_propfunc_t cb_newindex) {

    lua_class_property_t *prop;
    g_assert(token != L_TK_UNKNOWN);
    prop = g_new0(lua_class_property_t, 1);

    /* populate property */
    prop->new = cb_new;
    prop->index = cb_index;
    prop->newindex = cb_newindex;

    /* add property to class properties tree */
    g_hash_table_insert((GHashTable*) lua_class->properties,
            (gpointer) token, prop);
}

/** Creates a new Lua class.
 *
 * \param L The Lua VM state.
 * \param class The \ref lua_class_t that identifies the class.
 * \param name The name of the class and it's constructor in Lua.
 * \param allocator The constructor of the class.
 * \param index_miss_property The function to call when an unknown property was
 *          indexed. Can be NULL.
 * \param newindex_miss_property The function to call when an unknown property was
 *          set. Can be NULL.
 * \param methods The methods of the class's objects.
 * \param meta The methods of the class itself.
 */
void
luaH_class_setup(lua_State *L, lua_class_t *class,
        const gchar *name,
        lua_class_allocator_t allocator,
        lua_class_propfunc_t index_miss_property,
        lua_class_propfunc_t newindex_miss_property,
        const struct luaL_reg methods[],
        const struct luaL_reg meta[]) {
    /* Create the metatable */
    lua_newtable(L);
    /* Register it with class pointer as key in the registry */
    lua_pushlightuserdata(L, class);
    /* Duplicate the metatable */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_pushvalue(L, -1);           /* dup metatable                      2 */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable      1 */

    luaL_register(L, NULL, meta);                                      /* 1 */
    luaL_register(L, name, methods);                                   /* 2 */
    lua_pushvalue(L, -1);           /* dup self as metatable              3 */
    lua_setmetatable(L, -2);        /* set self as metatable              2 */
    lua_pop(L, 2);

    class->allocator = allocator;
    class->name = name;
    class->index_miss_property = index_miss_property;
    class->newindex_miss_property = newindex_miss_property;

    class->signals = signal_new();
    class->properties = (lua_class_property_array_t*) g_hash_table_new(
            g_direct_hash, g_direct_equal);

    if (!luaH_classes)
        luaH_classes = g_ptr_array_new();
    g_ptr_array_add(luaH_classes, class);
}

/** Generic wrapper around GTK's \c signal_add.
 * Will be used to implement the Lua \c add_signal method of a class.
 *
 * \param L The Lua VM state.
 * \param lua_class The class that handles the signal.
 * \param name The name of the signal.
 * \param ud The index of the handler function.
 */
void
luaH_class_add_signal(lua_State *L, lua_class_t *lua_class,
        const gchar *name, gint ud) {
    luaH_checkfunction(L, ud);
    signal_add(lua_class->signals, name, luaH_object_ref(L, ud));
}

/** Generic wrapper around GTK's \c signal_remove.
 * Will be used to implement the Lua \c remove_signal method of a class.
 *
 * \param L The Lua VM state.
 * \param lua_class The class that handles the signal.
 * \param name The name of the signal.
 * \param ud The index of the handler function.
 */
void
luaH_class_remove_signal(lua_State *L, lua_class_t *lua_class,
        const gchar *name, gint ud) {
    luaH_checkfunction(L, ud);
    gpointer ref = (gpointer) lua_topointer(L, ud);
    signal_remove(lua_class->signals, name, ref);
    luaH_object_unref(L, (gpointer) ref);
    lua_remove(L, ud);
}

/** Generic wrapper around GTK's \c signal_emit.
 * Will be used to implement the Lua \c emit_signal method of a class.
 *
 * \param L The Lua VM state.
 * \param lua_class The class that handles the signal.
 * \param name The name of the signal.
 * \param nargs The number of arguments the signal expects.
 * \param nret The number of return values the signal expects.
 */
gint
luaH_class_emit_signal(lua_State *L, lua_class_t *lua_class,
        const gchar *name, gint nargs, gint nret) {
    return signal_object_emit(L, lua_class->signals, name, nargs, nret);
}

gint
luaH_class_property_signal(lua_State *L, lua_class_t *lua_class,
        luakit_token_t tok)
{
    gchar *signame = g_strdup_printf("property::%s", token_tostring(tok));
    signal_object_emit(L, lua_class->signals, signame, 0, 0);
    g_free(signame);
    return 0;
}

/* Try to use the metatable of an object.
 * `idxobj` is the index of the object.
 * `idxfield` is the index of the field (attribute) to get.
 * Returns the number of element pushed on stack. */
gint
luaH_usemetatable(lua_State *L, gint idxobj, gint idxfield) {
    /* Get metatable of the object. */
    lua_getmetatable(L, idxobj);
    /* Get the field */
    lua_pushvalue(L, idxfield);
    lua_rawget(L, -2);
    /* Do we have a field like that? */
    if(!lua_isnil(L, -1)) {
        /* Yes, so return it! */
        lua_remove(L, -2);
        return 1;
    }
    /* No, so remove everything. */
    lua_pop(L, 2);

    return 0;
}

/** Get a property of an object.
 *
 * \param L The Lua VM state.
 * \param lua_class The class of the object.
 * \param fieldidx The index of the field name.
 * \return The object property if found, NULL otherwise.
 */
static lua_class_property_t *
luaH_class_property_get(lua_State *L, lua_class_t *lua_class, gint fieldidx) {
    const gchar *attr = luaL_checkstring(L, fieldidx);
    luakit_token_t token = l_tokenize(attr);

    return g_hash_table_lookup((GHashTable*) lua_class->properties,
            (gpointer) token);
}

/* Generic index meta function for objects.
 *
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 *
 * \luastack
 * \lvalue The object to index.
 * \lvalue The property to get.
 */
gint
luaH_class_index(lua_State *L) {
    /* Try to use metatable first. */
    if(luaH_usemetatable(L, 1, 2))
        return 1;

    lua_class_t *class = luaH_class_get(L, 1);

    lua_class_property_t *prop = luaH_class_property_get(L, class, 2);

    /* Property does exist and has an index callback */
    if(prop) {
        if(prop->index)
            return prop->index(L, luaH_checkudata(L, 1, class));
    } else {
        if(class->index_miss_property)
            return class->index_miss_property(L, luaH_checkudata(L, 1, class));
    }

    return 0;
}

/* Generic newindex meta function for objects.
 *
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 *
 * \luastack
 * \lvalue The object to index.
 * \lvalue The property to set.
 * \lvalue The value to set it to.
 */
gint
luaH_class_newindex(lua_State *L) {
    /* Try to use metatable first. */
    if(luaH_usemetatable(L, 1, 2))
        return 1;

    lua_class_t *class = luaH_class_get(L, 1);

    lua_class_property_t *prop = luaH_class_property_get(L, class, 2);

    /* Property does exist and has a newindex callback */
    if(prop) {
        if(prop->newindex)
            return prop->newindex(L, luaH_checkudata(L, 1, class));
    } else {
        if(class->newindex_miss_property)
            return class->newindex_miss_property(L, luaH_checkudata(L, 1, class));
    }

    return 0;
}

/** Generic constructor function for objects.
 * Returns the number of elements pushed on stack.
 *
 * \param L The Lua VM state.
 * \param lua_class The class of the new object.
 *
 * \luastack
 * \lvalue A table that contains properties of the new object.
 * \lreturn A new object derived from the given class.
 */
gint
luaH_class_new(lua_State *L, lua_class_t *lua_class) {
    gint idx = lua_gettop(L);

    /* Check we have a table that should contains some properties */
    luaH_checktable(L, idx);

    /* Create a new object */
    lua_object_t *object = lua_class->allocator(L);

    /* Push the first key before iterating */
    lua_pushnil(L);
    /* Iterate over the property keys */
    while(lua_next(L, idx)) {
        /* Check that the key is a string.
         * We cannot call tostring blindly or Lua will convert a key that is a
         * number TO A STRING, confusing lua_next() */
        if(lua_isstring(L, -2)) {
            /* Lookup the property */
            const char *attr = lua_tostring(L, -2);
            lua_class_property_t *prop = g_hash_table_lookup(
                    (GHashTable*) lua_class->properties,
                    (gpointer) l_tokenize(attr));

            if(prop && prop->new)
                prop->new(L, object);
        }
        /* Remove value */
        lua_pop(L, 1);
    }
    return 1;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
