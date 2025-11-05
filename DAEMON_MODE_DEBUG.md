# Rofi Daemon Mode - Debug Session Notes

## Overview
Working on daemon mode feature for rofi (branch: `feature/daemon-mode`). Daemon mode allows rofi to run as a persistent background process, with subsequent invocations forwarding to the daemon instead of spawning new instances.

## Fixes Applied (Commit: 30c98f23)

### 1. Fix dmenu stdin handling (rofi.c:1318)
**Problem**: When running `rofi -dmenu` with a daemon active, it would forward to the daemon but lose stdin data.

**Solution**: Added check to prevent dmenu mode from forwarding to daemon:
```c
gboolean is_dmenu = (find_arg("-dmenu") >= 0);
if (has_show && !is_dmenu && rofi_daemon_forward_show_request(show_mode)) {
```

### 2. Fix cleanup on daemon exit
**Problem**: Pidfile and socket not cleaned up when daemon exits.

**Solution**:
- Added pidfile removal in `rofi_daemon_cleanup()` (lines 1069-1071)
- Added SIGTERM signal handler (lines 1641-1642) so daemon responds to kill signals
```c
g_unix_signal_add(SIGTERM, main_loop_signal_handler_int, NULL);
```

### 3. Fix window not closing after selection
**Problem**: GUI window remained visible after selecting an item in daemon mode.

**Solution**: Added explicit `rofi_view_hide()` calls before cleanup in daemon mode (lines 296-297, 305-306).

**Note**: Order matters! Current implementation:
```c
rofi_view_remove_active(state);  // Remove from active first
if (daemon_mode) {
  rofi_view_hide();              // Then hide window
}
rofi_view_free(state);           // Then free state
```

## Root Cause Analysis

### Display/Surface Architecture Incompatibility with Daemon Mode

**Core Problem**: Rofi's display layer is designed for single-use (non-daemon) mode where the entire display/surface lifecycle happens once per invocation:
1. `display_setup()` - Create display connection & surface
2. Show window, handle input
3. `display_early_cleanup()` - **Destroy surface** (`wayland_surface_destroy()`)
4. `display_cleanup()` - Cleanup display connection

In daemon mode, we need to **reuse** the display/surface across multiple invocations, but:
- `rofi_view_hide()` → calls `display_early_cleanup()` → destroys surface
- Next invocation tries to create view → **CRASHES** (no surface exists)

**Attempted Fixes**:
1. ❌ Call `rofi_view_hide()` after each use → Crashes on second use (surface destroyed)
2. ❌ Don't call `rofi_view_hide()` → Window stays visible, blocks all input

**What's needed**: Either:
- Option A: Modify display layer to support "hide without destroy" for daemon mode
- Option B: Recreate surface for each daemon request (inefficient, may have timing issues)
- Option C: Keep surface alive but unmap/hide the layer shell surface

## Outstanding Issues

### CRITICAL: Window blocking input after use (current state)
**Symptom**: When pressing super+m twice in quick succession (opening rofi twice rapidly), the daemon either:
1. Segfaults, OR
2. Exits cleanly (exit code 0) but shouldn't exit at all

**Expected behavior**: Second request should be rejected with "Daemon request ignored: a view is already active" (see rofi.c:832-834)

**What we know**:
- Check exists at line 832: `if (rofi_view_get_active() != NULL)`
- Reordered cleanup to set active=NULL before hide/free
- But daemon still dies when tested with xremap keybinding (super+m)

**Possible causes to investigate**:
1. Race condition between removing active view and next request
2. `rofi_view_hide()` might be causing issues when called on non-active view
3. Signal handling issue causing unexpected exit
4. The view might not be properly set back to NULL after cleanup
5. Multiple simultaneous requests from xremap might be overwhelming the daemon

### Debug steps to try next:
1. Add debug logging to see exact execution flow:
   ```c
   g_debug("process_result: before remove_active");
   g_debug("process_result: active view is now: %p", rofi_view_get_active());
   ```

2. Check if `rofi_view_get_active()` properly returns NULL after `rofi_view_remove_active()`

3. Test with manual commands instead of xremap to isolate timing issues:
   ```bash
   rofi -daemon &
   sleep 1
   rofi -show drun &
   rofi -show drun &  # immediate second request
   ```

4. Consider adding mutex/lock around daemon show requests

5. Check if wayland vs X11 matters (wayland has different hide implementation)

## File Locations

- Main daemon code: `/home/admin/code/c/rofi/source/rofi.c`
- Daemon socket: `/run/user/1000/rofi-daemon.sock`
- Daemon pidfile: `/run/user/1000/rofi.pid`
- Build directory: `/home/admin/code/c/rofi/build/`

## Building and Testing

```bash
# Build
cd /home/admin/code/c/rofi
nix develop --command ninja -C build

# Test manually
./build/rofi -daemon &
./build/rofi -show drun

# Check daemon status
ps aux | grep "rofi.*daemon"
ls -la /run/user/$(id -u)/rofi*
```

## Home Manager Integration

rofi flake input in `~/.config/home-manager/flake.nix`:
```nix
rofi = {
  url = "git+https://github.com/sergioahp/rofi?ref=feature/daemon-mode&submodules=1";
  flake = false;
};
```

Overlay in `~/.config/home-manager/home.nix`:
```nix
rofi-unwrapped = p.rofi-unwrapped.overrideAttrs (oldAttrs: {
  src = inputs.rofi;
  version = "2.0.0-dev";
  doInstallCheck = false;
});
```

## Historical Context

Daemon mode was originally in rofi but removed in 2016 (commit 990914d2). The original implementation was quite different. This is a reimplementation.

## Next Session TODO

1. **PRIORITY**: Fix daemon crash/exit on rapid requests
2. Add proper logging to trace execution flow
3. Test race conditions with manual rapid requests
4. Consider adding request serialization/queuing
5. Test on both Wayland and X11 to see if behavior differs
6. Once stable, consider upstreaming to main rofi project
