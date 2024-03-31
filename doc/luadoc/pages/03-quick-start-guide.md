@name Quick-start Guide

# Quick-start Guide

## Using Luakit

If you've used browsers like vimperator, or vimium, you'll find yourself
right at home; most if not all luakit actions are available via keyboard
commands.

Just run:

    luakit [URI..]

Or to see the full list of luakit launch options run:

    luakit -h

### Opening the help page

To open the internal help page, press `<F1>`. From there, you can open
the introspector to view current keybindings or open the included API
documentation.

### Switching to command mode

Many common operations in luakit, such as typing and opening a URL, involve the
use of _commands_. This is done by switching to command mode, typing the
command, and pressing `<Enter>`.

To switch to command mode, press `:` (i.e. `<Shift-;>`), and the command
bar will appear at the bottom of the window. In the remainder of this document,
commands will be written prefixed by a `:`; so `:open` means to first press
`:`, and then type `open`.

### Go to a URL in the current tab

Type `:open` followed by a space and the URL to navigate to, and press `<Enter>`.
For example, to open the luakit website, type `:open luakit.github.io` and then
press press `<Enter>`.

_Shortcut_: press `o` to switch to command mode with `open ` already typed.

### Search

Press `/`, and type your search query, e.g: `/luakit`. Press `<Enter>`
to finish typing your search query and switch back to normal mode.
While in normal mode with highlighted search results, press `n` to
jump to the next search result, and `N` to jump to the previous one.

### Opening, closing, and undo-closing tabs

Try the following steps in order:

1. Run `:tabopen luakit.github.io`, and wait until the page finishes loading.
2. Press `d`, and the new tab will close.
3. Press `u`, and the just-closed tab will reappear.

### Scrolling in a web page

There are several different keys to scroll a webview:

 - The arrow, page up/down, and home/end keys all work as one would expect.
 - The `h`, `j`, `k`, `l`, `gg`, and `G` key bindings all scroll in a vim-like manner.
 - The `<Control-e>`, `<Control-y>`, `<Control-d>`, `<Control-u>`,
   `<Control-f>`, and `<Control-b>` keys all scroll vertically by various
   amounts.

### Bookmarks

 - Press `B` to bookmark the current page.
 - Press `gb` or `gB` to open the bookmarks page in the current tab or a
   new tab, respectively.

## Configuration

It is possible to configure most of the global Luakit settings in the
<a href="luakit://settings">luakit://settings</a> page. For further
adjustments (per-domain settings, adjusting/defining bindings or commands) you
should create a custom configuration file.

### Beginning a custom configuration

To customize luakit, you can define your own configuration by creating a
`userconf.lua` file in the `~/.config/luakit/` directory. This file
is loaded automatically by luakit, if it exists. Any changes made to it will
take effect after restarting luakit.

### Changing key bindings

To add/remove key bindings, use @ref{modes/add_binds} and @ref{remove_binds}
methods from the @ref{modes} module to do so.  For example, the following code
re-binds `<Control-c>` so the selected text gets copied to the clipboard:

    --- userconf.lua

    local modes = require "modes"

    modes.add_binds("normal", {{
        "<Control-c>",
        "Copy selected text.",
        function ()
            luakit.selection.clipboard = luakit.selection.primary
        end
    }})

If you just want to re-map an existing action to a new keybinding, you can use
the @ref{modes/remap_binds} method. For example:

    -- maps "<Control-p>" to the same action as "gT" (go to previous tab), and
    -- keeps "gT" binded as well
    modes.remap_binds("normal", {
        {"<Control-p>", "gT", true}
    })

You can also check all the currently available key bindings in the
<a href="luakit://binds">luakit://binds</a> page, for each of luakit's modes,
along with their documentation and links to the exact location where they
were defined.

### Changing the theme

Apart from `rc.lua`, the other configuration file is `theme.lua`, which
specifies the fonts and colours used by the interface widgets.

### Location of configuration files

When looking for a `.lua` file by name, luakit checks the following locations,
in the order specified here:

1. The current directory.
2. System directories for Lua files.
3. Luakit's collection of included modules (`/usr/share/luakit/lib/`).
4. The user's personal luakit configuration directory (`~/.config/luakit/`).
5. The system configuration directories (`/etc/xdg/luakit/`).

To list the directories luakit will search when loading modules, run the
following command:

    :lua w:notify(package.path:gsub(";","\n"))

<!-- vim: set et sw=4 ts=8 sts=4 tw=79 :-->
