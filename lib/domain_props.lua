--- Automatically apply per-domain webview properties.
--
-- This module allows you to have site-specific settings. For example, you can
-- choose to enable WebGL only on certain specific sites, or enable JavaScript
-- only on a sub-domain of a website without enabling JavaScript for the root
-- domain.
--
-- # Example `domain_props` rules
--
--     globals.domain_props = {
--         ["all"] = {
--             enable_scripts = false,
--             enable_plugins = false,
--         },
--         ["youtube.com"] = {
--             enable_scripts = true,
--         },
--         ["m.youtube.com"] = {
--             enable_scripts = false,
--         },
--     }
--
-- ## Explanation
--
-- There are three rules in the example. From top to bottom, they are
-- least-specific to most-specific:
--
--  - [m.youtube.com](https://m.youtube.com):  Any webpages on this domain will
--  have JavaScript disabled.
--  - [youtube.com](https://youtube.com): Any webpages on this
--  domain will have JavaScript enabled,_except for webpages on
--  [m.youtube.com](https://m.youtube.com)_. This is because the rule
--  for [m.youtube.com](https://m.youtube.com) is more specific than the
--  rule for [youtube.com](https://youtube.com), so its value for
--  `enable_scripts` is used for those sites.
--  - `all`: Any other webpages will have JavaScript disabled. In addition,
--  _all_ web pages will have plugins disabled, since no more-specific rules
--  specified a value for `enable_plugins`. This rule is less specific than all other rules.
--
-- # Rule application
--
-- The order that rules are specified in the file does not matter, although
-- in the default the "all" rule is listed first. All properties in _any_ matching
-- rules are applied, but the value that is used is the one specified in the
-- most specific rule. If a property is not applied in any rule, it is not
-- changed.
--
-- # Available properties
--
--  - `allow_modal_dialogs`
--  - `auto_load_images`
--  - `cursive_font_family`
--  - `default_charset`
--  - `default_font_family`
--  - `default_font_size`
--  - `default_monospace_font_size`
--  - `draw_compositing_indicators`
--  - `editable`
--  - `enable_accelerated_2d_canvas`
--  - `enable_caret_browsing`
--  - `enable_developer_extras`
--  - `enable_dns_prefetching`
--  - `enable_frame_flattening`
--  - `enable_fullscreen`
--  - `enable_html5_database`
--  - `enable_html5_local_storage`
--  - `enable_hyperlink_auditing`
--  - `enable_java`
--  - `enable_javascript`
--  - `enable_mediasource`
--  - `enable_media_stream`
--  - `enable_offline_web_application_cache`
--  - `enable_page_cache`
--  - `enable_plugins`
--  - `enable_private_browsing`
--  - `enable_resizable_text_areas`
--  - `enable_site_specific_quirks`
--  - `enable_smooth_scrolling`
--  - `enable_spatial_navigation`
--  - `enable_tabs_to_links`
--  - `enable_webaudio`
--  - `enable_webgl`
--  - `enable_write_console_messages_to_stdout`
--  - `enable_xss_auditor`
--  - `fantasy_font_family`
--  - `javascript_can_access_clipboard`
--  - `javascript_can_open_windows_automatically`
--  - `media_playback_allows_inline`
--  - `media_playback_requires_user_gesture`
--  - `minimum_font_size`
--  - `monospace_font_family`
--  - `pictograph_font_family`
--  - `print_backgrounds`
--  - `sans_serif_font_family`
--  - `serif_font_family`
--  - `user_agent`
--  - `zoom_level`
--  - `zoom_text_only`
--
-- @module domain_props
-- @author Mason Larobina
-- @copyright 2012 Mason Larobina <mason.larobina@gmail.com>

local _M = {}

msg.warn("domain_props.lua is deprecated, and will be removed in the next release!")
msg.warn("all functionality (and more!) has been moved to settings.lua")

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
