/*
 * web_context.c - WebKit web context setup and handling
 *
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

#include "globalconf.h"
#include "common/log.h"
#include "web_context.h"

#include <webkit2/webkit2.h>

/** WebKit context common to all web views */
static WebKitWebContext *web_context;
/** WebKit process count; default to unlimited */
static guint process_limit = 0;
/** Whether the web context startup function has been run */
static gboolean web_context_started = FALSE;

/** Defined in widgets/webview/downloads.c */
gboolean download_start_cb(WebKitWebContext *, WebKitDownload *, gpointer);

WebKitWebContext *
web_context_get(void)
{
    g_assert(web_context);
    return web_context;
}

guint
web_context_process_limit_get(void)
{
    return process_limit;
}

gboolean
web_context_process_limit_set(guint limit)
{
    if (web_context_started)
        return FALSE;
    process_limit = limit;
    return TRUE;
}

static void
website_data_manager_init(void)
{
    WebKitWebsiteDataManager *data_mgr = webkit_website_data_manager_new(
            "base-cache-directory", globalconf.cache_dir,
            "base-data-directory", globalconf.data_dir,
            NULL);

    web_context = webkit_web_context_new_with_website_data_manager(data_mgr);

    verbose("base_data_directory:                 %s", webkit_website_data_manager_get_base_data_directory(data_mgr));
    verbose("base_cache_directory:                %s", webkit_website_data_manager_get_base_cache_directory(data_mgr));
}

static void
web_context_set_default_spelling_language(void)
{
    /* This seems to autodetect spell checking languages */
    const gchar *null = NULL;
    webkit_web_context_set_spell_checking_languages(web_context, &null);
    gchar **ret = (gchar**)webkit_web_context_get_spell_checking_languages(web_context);
    if (!ret)
        return;
    gchar *langs = g_strjoinv(", ", ret);
    verbose("setting spell check languages: %s", langs);
    g_free(langs);
}

void
web_context_init(void)
{
    website_data_manager_init();
    webkit_web_context_set_favicon_database_directory(web_context, NULL);
    g_signal_connect(G_OBJECT(web_context), "download-started",
            G_CALLBACK(download_start_cb), NULL);

    /* Set default cookie policy: must match default in clib/soup.c */
    WebKitCookieManager *cookie_mgr = webkit_web_context_get_cookie_manager(web_context);
    webkit_cookie_manager_set_accept_policy(cookie_mgr, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

    web_context_set_default_spelling_language();
}

void
web_context_init_finish(void)
{
    if (web_context_started)
        return;

#if !WEBKIT_CHECK_VERSION(2,26,0)
    webkit_web_context_set_process_model(web_context, WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
    info("Web process count: %d", process_limit);
    webkit_web_context_set_web_process_count_limit(web_context, process_limit);
#endif

    web_context_started = TRUE;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
