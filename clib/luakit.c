/*
 * clib/luakit.c - Generic functions for Lua scripts
 *
 * Copyright © 2011 Mason Larobina <mason.larobina@gmail.com>
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

#include "clib/luakit.h"
#include "common/clib/luakit.h"
#include "clib/widget.h"
#include "common/luaserialize.h"
#include "common/luayield.h"
#include "common/ipc.h"
#include "common/resource.h"
#include "common/signal.h"
#include "ipc.h"
#include "luah.h"
#include "log.h"
#include "web_context.h"
#include "globalconf.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <webkit2/webkit2.h>

/* setup luakit module signals */
static lua_class_t luakit_class;
LUA_CLASS_FUNCS(luakit, luakit_class)

GtkClipboard *
luaH_clipboard_get(lua_State *L, gint idx)
{
#define CB_CASE(t) case L_TK_##t: return gtk_clipboard_get(GDK_SELECTION_##t);
    switch(l_tokenize(luaL_checkstring(L, idx)))
    {
      CB_CASE(PRIMARY)
      CB_CASE(SECONDARY)
      CB_CASE(CLIPBOARD)
      default: break;
    }
    return NULL;
#undef CB_CASE
}

/** __index metamethod for the luakit.selection table which
 * returns text from an X selection.
 * \see http://en.wikipedia.org/wiki/X_Window_selection
 * \see http://developer.gnome.org/gtk/stable/gtk-Clipboards.html#gtk-clipboard-wait-for-text
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack.
 */
static gint
luaH_luakit_selection_index(lua_State *L)
{
    GtkClipboard *selection = luaH_clipboard_get(L, 2);
    if (selection) {
        gchar *text = gtk_clipboard_wait_for_text(selection);
        if (text) {
            lua_pushstring(L, text);
            g_free(text);
            return 1;
        }
    }
    return 0;
}

/** __newindex metamethod for the luakit.selection table which
 * sets an X selection.
 * \see http://en.wikipedia.org/wiki/X_Window_selection
 * \see http://developer.gnome.org/gtk/stable/gtk-Clipboards.html#gtk-clipboard-set-text
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack (0).
 *
 * \lcode
 * luakit.selection.primary = "Malcolm Reynolds"
 * luakit.selection.clipboard = "John Crichton"
 * print(luakit.selection.primary) // outputs "Malcolm Reynolds"
 * luakit.selection.primary = nil  // clears the primary selection
 * print(luakit.selection.primary) // outputs nothing
 * \endcode
 */
static gint
luaH_luakit_selection_newindex(lua_State *L)
{
    GtkClipboard *selection = luaH_clipboard_get(L, 2);
    if (selection) {
        const gchar *text = !lua_isnil(L, 3) ? luaL_checkstring(L, 3) : NULL;
        if (text && *text)
            gtk_clipboard_set_text(selection, text, -1);
        else
            gtk_clipboard_clear(selection);
    }
    return 0;
}

static gint
luaH_luakit_selection_table_push(lua_State *L)
{
    /* create selection table */
    lua_newtable(L);
    /* setup metatable */
    lua_createtable(L, 0, 2);
    lua_pushliteral(L, "__index");
    lua_pushcfunction(L, luaH_luakit_selection_index);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__newindex");
    lua_pushcfunction(L, luaH_luakit_selection_newindex);
    lua_rawset(L, -3);
    lua_setmetatable(L, -2);
    return 1;
}

/** Shows a Gtk save dialog.
 * \see http://developer.gnome.org/gtk/stable/GtkDialog.html
 *
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 *
 * \luastack
 * \lparam title          The title of the dialog window.
 * \lparam parent         The parent window of the dialog or \c nil.
 * \lparam default_folder The folder to initially display in the file dialog.
 * \lparam default_name   The filename to preselect in the dialog.
 * \lreturn               The name of the selected file or \c nil if the
 *                        dialog was cancelled.
 */
static gint
luaH_luakit_save_file(lua_State *L)
{
    const gchar *title = luaL_checkstring(L, 1);

    /* get window to display dialog over */
    GtkWindow *parent_window = NULL;
    if (!lua_isnil(L, 2)) {
        widget_t *parent = luaH_checkudata(L, 2, &widget_class);
        if (!GTK_IS_WINDOW(parent->widget))
            luaL_argerror(L, 2, "window widget");
        parent_window = GTK_WINDOW(parent->widget);
    }

    const gchar *default_folder = luaL_checkstring(L, 3);
    const gchar *default_name = luaL_checkstring(L, 4);

#if GTK_CHECK_VERSION(3,10,0)
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title,
            parent_window,
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Save", GTK_RESPONSE_ACCEPT,
            NULL);
#else
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title,
            parent_window,
            GTK_FILE_CHOOSER_ACTION_SAVE,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
            NULL);
#endif

    /* set default folder, name and overwrite confirmation policy */
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), default_folder);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_name);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        lua_pushstring(L, filename);
        g_free(filename);
    } else
        lua_pushnil(L);

    gtk_widget_destroy(dialog);
    return 1;
}

/** Executes a child synchronously (waits for the child to exit before
 * returning). The exit status and all stdout and stderr output from the
 * child is returned.
 * \see http://developer.gnome.org/glib/stable/glib-Spawning-Processes.html#g-spawn-command-line-sync
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack (3).
 *
 * \luastack
 * \lparam cmd The command to run (from a shell).
 * \lreturn The exit status of the child.
 * \lreturn The childs stdout.
 * \lreturn The childs stderr.
 */
static gint
luaH_luakit_spawn_sync(lua_State *L)
{
    GError *e = NULL;
    gchar *_stdout = NULL;
    gchar *_stderr = NULL;
    gint rv;
    struct sigaction sigact;
    struct sigaction oldact;

    const gchar *command = luaL_checkstring(L, 1);

    /* Note: we have to temporarily clear the SIGCHLD handler. Otherwise
     * g_spawn_sync wouldn't be able to read subprocess' return value. */
    sigact.sa_handler = SIG_DFL;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    if (sigaction(SIGCHLD, &sigact, &oldact))
        fatal("Can't clear SIGCHLD handler");

    g_spawn_command_line_sync(command, &_stdout, &_stderr, &rv, &e);

    /* restore SIGCHLD handler */
    if (sigaction(SIGCHLD, &oldact, NULL))
        fatal("Can't restore SIGCHLD handler");

    /* raise error on spawn function error */
    if (e) {
        lua_pushstring(L, e->message);
        g_clear_error(&e);
        lua_error(L);
    }

    /* push exit status, stdout, stderr on to stack and return */
    lua_pushinteger(L, WEXITSTATUS(rv));
    lua_pushstring(L, _stdout);
    lua_pushstring(L, _stderr);
    g_free(_stdout);
    g_free(_stderr);
    return 3;
}

/* Calls the Lua function defined as callback for a (async) spawned process
 * The called Lua function receives 2 arguments:
 * Exit type: one of: "exit" (normal exit), "signal" (terminated by
 *            signal), "unknown" (another reason)
 * Exit number: When normal exit happened, the exit code of the process. When
 *              finished by a signal, the signal number. -1 otherwise.
 */
void
async_callback_handler(GPid pid, gint status, gpointer cb_ref)
{
    g_spawn_close_pid(pid);
    if (!cb_ref)
        return;

    lua_State *L = common.L;

    /* push exit reason & exit status onto lua stack */
    if (WIFEXITED(status)) {
        lua_pushliteral(L, "exit");
        lua_pushinteger(L, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        lua_pushliteral(L, "signal");
        lua_pushinteger(L, WTERMSIG(status));
    } else {
        lua_pushliteral(L, "unknown");
        lua_pushinteger(L, -1);
    }

    /* push callback function onto stack */
    luaH_object_push(L, cb_ref);
    luaH_dofunction(L, 2, 0);
    luaH_object_unref(L, cb_ref);
}

/** Executes a child program asynchronously (your program will not block waiting
 * for the child to exit).
 *
 * \see \ref async_callback_handler
 * \see http://developer.gnome.org/glib/stable/glib-Shell-related-Utilities.html#g-shell-parse-argv
 * \see http://developer.gnome.org/glib/stable/glib-Spawning-Processes.html#g-spawn-async
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack (0).
 *
 * \luastack
 * \lparam command  The command to execute a child program.
 * \lparam callback Optional Lua callback function.
 * \lreturn The child pid.
 *
 * \lcode
 * local editor = "gvim"
 * local filename = "config"
 *
 * function editor_callback(exit_reason, exit_status)
 *     if exit_reason == "exit" then
 *         print(string.format("Contents of %q:", filename))
 *         for line in io.lines(filename) do
 *             print(line)
 *         end
 *     else
 *         print("Editor exited with status: " .. exit_status)
 *     end
 * end
 *
 * luakit.spawn(string.format("%s %q", editor, filename), editor_callback)
 * \endcode
 */
static gint
luaH_luakit_spawn(lua_State *L)
{
    GError *e = NULL;
    GPid pid = 0;
    const gchar *command = luaL_checkstring(L, 1);
    gint argc = 0;
    gchar **argv = NULL;
    gpointer cb_ref = NULL;

    /* check callback function type */
    if (lua_gettop(L) > 1 && !lua_isnil(L, 2)) {
        if (lua_isfunction(L, 2))
            cb_ref = luaH_object_ref(L, 2);
        else
            luaL_typerror(L, 2, lua_typename(L, LUA_TFUNCTION));
    }

    /* parse arguments */
    if (!g_shell_parse_argv(command, &argc, &argv, &e))
        goto spawn_error;

    /* spawn command */
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL,
            NULL, &pid, &e))
        goto spawn_error;

    /* call Lua callback (if present), and free GLib resources */
    g_child_watch_add(pid, async_callback_handler, cb_ref);

    g_strfreev(argv);
    lua_pushnumber(L, pid);
    return 1;

spawn_error:
    luaH_object_unref(L, cb_ref);
    lua_pushstring(L, e->message);
    g_clear_error(&e);
    g_strfreev(argv);
    lua_error(L);
    return 0;
}

/** Wrapper around the execl POSIX function. The exec family of functions
 * replaces the current process image with a new process image. This function
 * will only return if there was an error with the execl call.
 * \see http://en.wikipedia.org/wiki/Execl
 *
 * \param L The Lua VM state.
 * \return  The number of elements pushed on the stack (0).
 */
static gint
luaH_luakit_exec(lua_State *L)
{
    static const gchar *shell = NULL;
    if (!shell && !(shell = g_getenv("SHELL")))
        shell = "/bin/sh";
    ipc_remove_socket_file();
    execl(shell, shell, "-c", luaL_checkstring(L, 1), NULL);
    return 0;
}

/** Pushes the command-line options parsed by luakit
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack.
 */
static gint
luaH_luakit_push_options_table(lua_State *L)
{
    lua_newtable(L);
    for (guint i = 0; i < globalconf.argv->len; ++i) {
        lua_pushstring(L, g_ptr_array_index(globalconf.argv, i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static WebKitWebsiteDataTypes
luaH_parse_website_data_types_table(lua_State *L, gint idx)
{
    WebKitWebsiteDataTypes types = 0;
    idx = luaH_absindex(L, idx);
    luaH_checktable(L, idx);
    size_t len = lua_objlen(L, idx);
    for (size_t i = 1; i <= len; i++) {
        lua_rawgeti(L, idx, i);
        if (!lua_isstring(L, -1))
            luaL_error(L, "website data types must be strings");
        const char *type = lua_tostring(L, -1);

#define TYPE(upper, lower) \
        if (g_str_equal(type, #lower)) types |= WEBKIT_WEBSITE_DATA_##upper;
        TYPE(MEMORY_CACHE, memory_cache)
        TYPE(DISK_CACHE, disk_cache)
        TYPE(OFFLINE_APPLICATION_CACHE, offline_application_cache)
        TYPE(SESSION_STORAGE, session_storage)
        TYPE(LOCAL_STORAGE, local_storage)
        TYPE(INDEXEDDB_DATABASES, indexeddb_databases)
        TYPE(PLUGIN_DATA, plugin_data)
        TYPE(COOKIES, cookies)
#if WEBKIT_CHECK_VERSION(2,24,0)
        TYPE(DEVICE_ID_HASH_SALT, device_id_hash_salt)
#endif
#if WEBKIT_CHECK_VERSION(2,26,0)
        TYPE(HSTS_CACHE, hsts_cache)
#endif
        TYPE(ALL, all)
#undef TYPE

        lua_pop(L, 1);
    }

    return types;
}

static void
website_data_fetch_finish(WebKitWebsiteDataManager *manager, GAsyncResult *result, lua_State *L)
{
    g_assert_cmpint(lua_status(L),==,LUA_YIELD);
    GError *error = NULL;
    GList *items = webkit_website_data_manager_fetch_finish(manager, result, &error);
    if (error) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
    } else {
        lua_newtable(L);
        GList *item = items;
        while (item) {
            WebKitWebsiteData *website_data = item->data;
            WebKitWebsiteDataTypes present = webkit_website_data_get_types(website_data);

            lua_pushstring(L, webkit_website_data_get_name(website_data));
            lua_newtable(L);

#define TYPE(upper, lower)                                                    \
            if (present & WEBKIT_WEBSITE_DATA_##upper) {                      \
                lua_pushstring(L, #lower);                                    \
                lua_pushinteger(L, webkit_website_data_get_size(website_data, \
                            WEBKIT_WEBSITE_DATA_##upper));                    \
                lua_rawset(L, -3);                                            \
            }
            TYPE(MEMORY_CACHE, memory_cache)
            TYPE(DISK_CACHE, disk_cache)
            TYPE(OFFLINE_APPLICATION_CACHE, offline_application_cache)
            TYPE(SESSION_STORAGE, session_storage)
            TYPE(LOCAL_STORAGE, local_storage)
            TYPE(INDEXEDDB_DATABASES, indexeddb_databases)
            TYPE(PLUGIN_DATA, plugin_data)
            TYPE(COOKIES, cookies)
#if WEBKIT_CHECK_VERSION(2,24,0)
            TYPE(DEVICE_ID_HASH_SALT, device_id_hash_salt)
#endif
#if WEBKIT_CHECK_VERSION(2,26,0)
            TYPE(HSTS_CACHE, hsts_cache)
#endif
#undef TYPE
            lua_rawset(L, -3);

            webkit_website_data_unref(website_data);
            item = item->next;
        }
    }

    g_list_free(items);
    luaH_resume(L, lua_gettop(L));
}

static gint
luaH_luakit_website_data_fetch(lua_State *L)
{
    WebKitWebsiteDataTypes data_types = luaH_parse_website_data_types_table(L, 1);

    if (data_types == 0)
        return luaL_error(L, "no website data types specified");

    WebKitWebContext *web_context = web_context_get();
    WebKitWebsiteDataManager *data_manager = webkit_web_context_get_website_data_manager(web_context);
    webkit_website_data_manager_fetch(data_manager, data_types, NULL,
            (GAsyncReadyCallback)website_data_fetch_finish, L);

    return luaH_yield(L);
}

typedef struct _website_data_remove_task_t {
    lua_State *L;
    WebKitWebsiteDataTypes data_types;
    char *domain;
} website_data_remove_task_t;

static void
website_data_remove_finish(WebKitWebsiteDataManager *manager, GAsyncResult *result, website_data_remove_task_t *wdrt)
{
    lua_State *L = wdrt->L;
    g_assert_cmpint(lua_status(L),==,LUA_YIELD);

    GError *error = NULL;
    webkit_website_data_manager_remove_finish(manager, result, &error);
    if (error) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
    } else
        lua_pushboolean(L, TRUE);

    g_free(wdrt->domain);
    g_slice_free(website_data_remove_task_t, wdrt);
    luaH_resume(L, lua_gettop(L));
}

static void
luaH_luakit_website_data_remove_cont(WebKitWebsiteDataManager *manager, GAsyncResult *result, website_data_remove_task_t *wdrt)
{
    lua_State *L = wdrt->L;
    g_assert_cmpint(lua_status(L),==,LUA_YIELD);

    GError *error = NULL;
    GList *items = webkit_website_data_manager_fetch_finish(manager, result, &error);
    if (error) {
        lua_pushstring(L, error->message);
        g_error_free(error);
        g_free(wdrt->domain);
        g_slice_free(website_data_remove_task_t, wdrt);
        luaL_error(L, lua_tostring(L, -1));
    }

    GList *item = items;
    while (item) {
        WebKitWebsiteData *website_data = item->data;
        GList *next = item->next;
        if (!g_str_equal(webkit_website_data_get_name(website_data), wdrt->domain)) {
            webkit_website_data_unref(website_data);
            items = g_list_delete_link(items, item);
        }
        item = next;
    }

    if (!items) {
        g_free(wdrt->domain);
        g_slice_free(website_data_remove_task_t, wdrt);
        lua_pushboolean(L, TRUE);
        luaH_resume(L, 1);
        return;
    }

    WebKitWebContext *web_context = web_context_get();
    WebKitWebsiteDataManager *data_manager = webkit_web_context_get_website_data_manager(web_context);
    webkit_website_data_manager_remove(data_manager, wdrt->data_types, items, NULL,
            (GAsyncReadyCallback)website_data_remove_finish, wdrt);

    item = items;
    while (item) {
        webkit_website_data_unref(item->data);
        item = item->next;
    }
    g_list_free(items);
}

static gint
luaH_luakit_website_data_remove(lua_State *L)
{
    WebKitWebsiteDataTypes data_types = luaH_parse_website_data_types_table(L, 1);
    if (data_types == 0)
        return luaL_error(L, "no website data types specified");
    const char *domain = luaL_checkstring(L, 2);

    website_data_remove_task_t *wdrt = g_slice_new0(website_data_remove_task_t);
    wdrt->L = L;
    wdrt->domain = g_strdup(domain);
    wdrt->data_types = data_types;

    WebKitWebContext *web_context = web_context_get();
    WebKitWebsiteDataManager *data_manager = webkit_web_context_get_website_data_manager(web_context);
    webkit_website_data_manager_fetch(data_manager, data_types, NULL,
            (GAsyncReadyCallback)luaH_luakit_website_data_remove_cont, wdrt);

    return luaH_yield(L);
}

static void
website_data_clear_finish(WebKitWebsiteDataManager *manager, GAsyncResult *result, lua_State *L)
{
    g_assert_cmpint(lua_status(L),==,LUA_YIELD);

    GError *error = NULL;
    webkit_website_data_manager_clear_finish(manager, result, &error);
    if (error) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
    } else
        lua_pushboolean(L, TRUE);

    luaH_resume(L, lua_gettop(L));
}

static gint
luaH_luakit_website_data_clear(lua_State *L)
{
    WebKitWebsiteDataTypes data_types = luaH_parse_website_data_types_table(L, 1);
    if (data_types == 0)
        return luaL_error(L, "no website data types specified");
    GTimeSpan timespan = luaL_optinteger(L, 2, 0);

    WebKitWebContext *web_context = web_context_get();
    WebKitWebsiteDataManager *data_manager = webkit_web_context_get_website_data_manager(web_context);
    webkit_website_data_manager_clear(data_manager, data_types, timespan, NULL,
            (GAsyncReadyCallback)website_data_clear_finish, L);

    return luaH_yield(L);
}

static gint
luaH_luakit_website_data_index(lua_State *L)
{
    const gchar *prop = luaL_checkstring(L, 2);
    luakit_token_t token = l_tokenize(prop);
    switch (token) {
        case L_TK_FETCH:
            lua_pushcfunction(L, luaH_luakit_website_data_fetch);
            luaH_yield_wrap_function(L);
            return 1;
        case L_TK_REMOVE:
            lua_pushcfunction(L, luaH_luakit_website_data_remove);
            luaH_yield_wrap_function(L);
            return 1;
        case L_TK_CLEAR:
            lua_pushcfunction(L, luaH_luakit_website_data_clear);
            luaH_yield_wrap_function(L);
            return 1;
        default: return 0;
    }
}

static gint
luaH_luakit_push_website_data_table(lua_State *L)
{
    lua_newtable(L);
    /* setup metatable */
    lua_createtable(L, 0, 2);
    /* push __index metafunction */
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, 1); /* copy webview userdata */
    lua_pushcclosure(L, luaH_luakit_website_data_index, 1);
    lua_rawset(L, -3);
    lua_setmetatable(L, -2);
    return 1;
}

static gint
luaH_string_wch_convert_case(lua_State *L, const char *key, gboolean upper)
{
    guint kval = gdk_keyval_from_name(key);
    if (kval == GDK_KEY_VoidSymbol) {
        debug("unrecognized key symbol '%s'", key);
        lua_pushstring(L, key);
        return 1;
    }
    guint cased;
    gdk_keyval_convert_case(kval, upper ? NULL : &cased, upper ? &cased : NULL);
    luaH_keystr_push(L, cased);
    return 1;
}

static gint
luaH_luakit_wch_lower(lua_State *L)
{
    return luaH_string_wch_convert_case(L, luaL_checkstring(L, 1), FALSE);
}

static gint
luaH_luakit_wch_upper(lua_State *L)
{
    return luaH_string_wch_convert_case(L, luaL_checkstring(L, 1), TRUE);
}

static gint
luaH_luakit_clear_favicon_database(lua_State *UNUSED(L))
{
    WebKitWebContext *ctx = web_context_get();
    WebKitFaviconDatabase *fdb = webkit_web_context_get_favicon_database(ctx);
    webkit_favicon_database_clear(fdb);
    return 0;
}

static gint
luaH_luakit_push_install_paths_table(lua_State *L)
{
    lua_createtable(L, 0, 6);
    lua_pushliteral(L, LUAKIT_INSTALL_PATH);
    lua_setfield(L, -2, "install_dir");
    lua_pushliteral(L, LUAKIT_CONFIG_PATH);
    lua_setfield(L, -2, "config_dir");
    lua_pushliteral(L, LUAKIT_DOC_PATH);
    lua_setfield(L, -2, "doc_dir");
    lua_pushliteral(L, LUAKIT_MAN_PATH);
    lua_setfield(L, -2, "man_dir");
    lua_pushliteral(L, LUAKIT_PIXMAP_PATH);
    lua_setfield(L, -2, "pixmap_dir");
    lua_pushliteral(L, LUAKIT_APP_PATH);
    lua_setfield(L, -2, "app_dir");
    return 1;
}

/** luakit module index metamethod.
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack.
 */
static gint
luaH_luakit_index(lua_State *L)
{
    if (luaH_usemetatable(L, 1, 2))
        return 1;

    widget_t *w;
    const gchar *prop = luaL_checkstring(L, 2);
    luakit_token_t token = l_tokenize(prop);

    switch (token) {

      /* push string properties */
      PS_CASE(CACHE_DIR,        globalconf.cache_dir)
      PS_CASE(CONFIG_DIR,       globalconf.config_dir)
      PS_CASE(XDGPREFIX,        LUAKIT_CONFIG_PATH)
      PS_CASE(DATA_DIR,         globalconf.data_dir)
      PS_CASE(EXECPATH,         globalconf.execpath)
      PS_CASE(CONFPATH,         globalconf.confpath)
      PS_CASE(RESOURCE_PATH,    resource_path_get())
      /* push boolean properties */
      PB_CASE(VERBOSE,          log_get_verbosity("all") >= LOG_LEVEL_verbose)
      PB_CASE(NOUNIQUE,         globalconf.nounique)
      PB_CASE(ENABLE_SPELL_CHECKING,    webkit_web_context_get_spell_checking_enabled(web_context_get()))
      /* push integer properties */
      PI_CASE(PROCESS_LIMIT,    web_context_process_limit_get())
      case L_TK_OPTIONS:
        return luaH_luakit_push_options_table(L);
      case L_TK_WEBSITE_DATA:
        return luaH_luakit_push_website_data_table(L);

      PB_CASE(WEBKIT2,          true)

      case L_TK_WINDOWS:
        lua_newtable(L);
        for (guint i = 0; i < globalconf.windows->len; i++) {
            w = globalconf.windows->pdata[i];
            luaH_object_push(L, w->ref);
            lua_rawseti(L, -2, i + 1);
        }
        return 1;

      case L_TK_WEBKIT_VERSION:
        lua_pushfstring(L, "%d.%d.%d", WEBKIT_MAJOR_VERSION,
                WEBKIT_MINOR_VERSION, WEBKIT_MICRO_VERSION);
        return 1;

      case L_TK_WEBKIT_USER_AGENT_VERSION:
        lua_pushfstring(L, "%d.%d", WEBKIT_MAJOR_VERSION,
                WEBKIT_MINOR_VERSION);
        return 1;

      case L_TK_SELECTION:
        return luaH_luakit_selection_table_push(L);

      case L_TK_INSTALL_PATH:
        warn("luakit.install_path is deprecated: use luakit.install_paths.install_dir instead");
        lua_pushliteral(L, LUAKIT_INSTALL_PATH);
        return 1;

      case L_TK_INSTALL_PATHS:
        return luaH_luakit_push_install_paths_table(L);

      case L_TK_VERSION:
        lua_pushliteral(L, VERSION);
        return 1;

      case L_TK_DEV_PATHS:
#ifdef DEVELOPMENT_PATHS
        lua_pushboolean(L, TRUE);
#else
        lua_pushboolean(L, FALSE);
#endif
        return 1;

      case L_TK_SPELL_CHECKING_LANGUAGES:
        luaH_push_strv(L, webkit_web_context_get_spell_checking_languages(web_context_get()));
        return 1;

      default:
        break;
    }
    return 0;
}

/** luakit module newindex metamethod.
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack.
 */
static gint
luaH_luakit_newindex(lua_State *L)
{
    if (!lua_isstring(L, 2))
        return 0;
    luakit_token_t token = l_tokenize(lua_tostring(L, 2));

    switch (token) {
        case L_TK_PROCESS_LIMIT:
            if (!web_context_process_limit_set(lua_tointeger(L, 3)))
                return luaL_error(L, "Too late to set WebKit process limit");
            break;
        case L_TK_ENABLE_SPELL_CHECKING:
            webkit_web_context_set_spell_checking_enabled(web_context_get(),
                    luaH_checkboolean(L, 3));
            break;
        case L_TK_SPELL_CHECKING_LANGUAGES: {
            const gchar ** langs = luaH_checkstrv(L, 3);
            WebKitWebContext *ctx = web_context_get();
            webkit_web_context_set_spell_checking_languages(ctx, langs);
            const gchar * const * accepted = webkit_web_context_get_spell_checking_languages(ctx);
            for (const gchar ** lang = langs; *lang; lang++)
                if (!g_strv_contains(accepted, *lang))
                    warn("unrecognized language code '%s'", *lang);
            g_free(langs);
            break;
        }
        case L_TK_RESOURCE_PATH:
            resource_path_set(luaL_checkstring(L, 3));
            break;
        default:
            return 0;
    }

    return 0;
}

/** Quit the main GTK loop.
 * \see http://developer.gnome.org/gtk/stable/gtk-General.html#gtk-main-quit
 *
 * \param  L The Lua VM state.
 * \return   The number of elements pushed on stack.
 */
static gint
luaH_luakit_quit(lua_State *UNUSED(L))
{
    if (gtk_main_level())
        gtk_main_quit();
    else
        exit(EXIT_SUCCESS);
    return 0;
}

/** Defined in widgets/webview.c */
void luakit_uri_scheme_request_cb(WebKitURISchemeRequest *, gpointer);

static gint
luaH_luakit_register_scheme(lua_State *L)
{
    const gchar *scheme = luaL_checkstring(L, 1);

    if (g_str_equal(scheme, ""))
        return luaL_error(L, "scheme cannot be empty");
    if (g_str_equal(scheme, "http") || g_str_equal(scheme, "https"))
        return luaL_error(L, "scheme cannot be 'http' or 'https'");
    if (!g_regex_match_simple("^[a-z][a-z0-9\\+\\-\\.]*$", scheme, 0, 0))
        return luaL_error(L, "scheme must match [a-z][a-z0-9\\+\\-\\.]*");

    webkit_web_context_register_uri_scheme(web_context_get(), scheme,
            (WebKitURISchemeRequestCallback) luakit_uri_scheme_request_cb,
            g_strdup(scheme), g_free);
    return 0;
}

gint
luaH_luakit_allow_certificate(lua_State *L)
{
    const gchar *host = luaL_checkstring(L, 1);
    size_t len;
    const gchar *cert_pem = luaL_checklstring(L, 2, &len);
    GError *err = NULL;

    GTlsCertificate *cert = g_tls_certificate_new_from_pem(cert_pem, len, &err);

    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err->message);
        return 2;
    }

    WebKitWebContext *ctx = web_context_get();
    webkit_web_context_allow_tls_certificate_for_host(ctx, cert, host);
    g_object_unref(G_OBJECT(cert));

    lua_pushboolean(L, TRUE);
    return 1;
}

gint
luaH_class_index_miss_property(lua_State *L, lua_object_t* UNUSED(obj))
{
    signal_object_emit(L, luakit_class.signals, "debug::index::miss", 2, 0);
    return 0;
}

gint
luaH_class_newindex_miss_property(lua_State *L, lua_object_t* UNUSED(obj))
{
    signal_object_emit(L, luakit_class.signals, "debug::newindex::miss", 3, 0);
    return 0;
}

/** Setup luakit module.
 *
 * \param L The Lua VM state.
 */
void
luakit_lib_setup(lua_State *L)
{
    static const struct luaL_Reg luakit_lib[] =
    {
        LUA_CLASS_METHODS(luakit)
        LUAKIT_LIB_COMMON_METHODS
        { "__index",                luaH_luakit_index },
        { "__newindex",             luaH_luakit_newindex },
        { "exec",                   luaH_luakit_exec },
        { "quit",                   luaH_luakit_quit },
        { "save_file",              luaH_luakit_save_file },
        { "spawn",                  luaH_luakit_spawn },
        { "spawn_sync",             luaH_luakit_spawn_sync },
        { "register_scheme",        luaH_luakit_register_scheme },
        { "allow_certificate",      luaH_luakit_allow_certificate },
        { "wch_lower",              luaH_luakit_wch_lower },
        { "wch_upper",              luaH_luakit_wch_upper },
        { "clear_favicon_database", luaH_luakit_clear_favicon_database },
        { NULL,                     NULL }
    };

    /* create signals array */
    luakit_class.signals = signal_new();

    /* export luakit lib */
    luaH_openlib(L, "luakit", luakit_lib, luakit_lib);
}

lua_class_t *
luakit_lib_get_luakit_class(void)
{
    return &luakit_class;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
