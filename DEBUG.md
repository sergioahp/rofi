# Rofi Daemon Debug Session Notes

## Environment Setup
- Added Python + PyGObject support to the dev shell (`flake.nix`).
- Included GLib/GDK/Pango/Cairo/Wayland typelibs via gobject-introspection hook.
- Hook now prints `GI_TYPELIB_PATH` on entry for visibility.
- Python script `probe_gio_socketservice.py` installs a hook on `Gio.SocketService.new` and logs incoming connections.
- Nix dev shell makes `python3` expose `gi.repository.Gio` without extra manual exports.

## MCP + Tooling
- Cloned `mcp-gdb`, ran `npm install && npm run build`.
- Added server entry to `~/.local/state/openai/mcp/config.json`.
- Updated `~/.codex/config.toml` to enable the `gdb` MCP server with `network=false` and `sandbox_mode=workspace-write`.

## Reproduction Steps
1. Start daemon under GDB with env vars (DISPLAY=:1, WAYLAND_DISPLAY=wayland-1, XDG_RUNTIME_DIR=/run/user/1000).
2. Run `./build/rofi -show window` from another shell.
3. Daemon accepts one connection, processes command, then socket is closed. Next client attempts fail with ECONNREFUSED and daemon eventually aborts due to GLib critical (g_object_unref on non-object).
4. Backtrace with `G_DEBUG=fatal-warnings` shows crash in `rofi_icon_fetcher_worker` when daemon is killed by warning handler after connection failure.
5. Even without GDB, running `./build/rofi -daemon` followed by `./build/rofi -show window` twice triggers GLib assertion “g_object_unref: assertion 'G_IS_OBJECT (object)' failed” and daemon exits.

## Current Code Changes
- Added `rofi_is_daemon_mode` guard in `rofi_view_trigger_global_action` to avoid dereferencing NULL state.
- Added daemon socket management, request forwarding, and keep-alive logic in `source/rofi.c`.
- Prevented `rofi_view_maybe_update` from quitting main loop when daemon mode active.
- Added dev shell support for python/gi (flake).
- Added `mcp-gdb` ignore entry in `.gitignore`.

## Outstanding Issues
- Daemon crashes on repeated client connections (`g_object_unref` critical). Needs investigation – likely connection management or worker thread cleanup.
- ECONNREFUSED occurs after first connection; socket service likely stops or closes connections unexpectedly.
- Need to run daemon under GDB with warnings promoted to fatal to get precise fault (requires stable reproduction; current attempts hang/fail due to GLib warnings during icon fetch).
- Need to push `flake.nix`, `.gitignore`, `probe_gio_socketservice.py`, `DEBUG.md`, and code changes on branch `feature/daemon-mode`.

## Next Steps
1. Commit outstanding changes.
2. Push branch (`git push -f origin feature/daemon-mode`).
3. Investigate `rofi_icon_fetcher_worker` warnings under daemon mode.
4. Confirm MCP `gdb` server works on other machines with updated config.

