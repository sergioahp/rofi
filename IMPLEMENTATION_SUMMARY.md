# DRun Launch Prefix - Implementation Summary

## ✅ Feature Complete

A new `drun-launch-prefix` configuration option has been successfully implemented for rofi's drun mode.

## What Was Built

### Problem Solved
On Wayland with workspace managers (like Hyprland + uwsm), switching workspaces while an app is launching causes the app to open on the original workspace instead of following the user.

### Solution
Added a configuration option to prepend a command prefix when launching applications from drun mode, enabling integration with session managers and sandboxing tools.

## Implementation Details

### Files Modified (5 files, 205 insertions, 1 deletion)

1. **`include/settings.h`** (Line 141)
   - Added `char *drun_launch_prefix;` field to Settings struct

2. **`source/xrmoptions.c`** (Lines 271-276)
   - Registered `drun-launch-prefix` configuration option
   - Added help text with examples

3. **`config/config.c`** (Lines 145-146)
   - Set default value to `NULL` (feature disabled by default)

4. **`source/modes/drun.c`** (Lines 513-528)
   - Applied prefix in `exec_cmd_entry()` function
   - Proper memory management with g_strdup_printf/g_free
   - Only affects drun mode, not run/ssh/etc.

5. **`DRUN_WRAPPER_DESIGN.md`** (New file)
   - Complete design documentation
   - Research findings
   - Alternative implementations
   - Use case examples

### Code Changes

**In exec_cmd_entry() (drun.c):**
```c
// Apply launch prefix if configured
const gchar *command_to_launch = fp;
gchar *prefixed_command = NULL;
if (config.drun_launch_prefix != NULL &&
    strlen(config.drun_launch_prefix) > 0) {
  prefixed_command = g_strdup_printf("%s %s",
                                     config.drun_launch_prefix, fp);
  command_to_launch = prefixed_command;
}

launched = helper_execute_command(exec_path, command_to_launch, terminal, sn ? &context : NULL);

// Free prefixed command if it was allocated
if (prefixed_command != NULL) {
  g_free(prefixed_command);
}
```

## Usage Examples

### Command Line
```bash
# With uwsm
rofi -show drun -drun-launch-prefix "uwsm app --"

# With systemd
rofi -show drun -drun-launch-prefix "systemd-run --user --scope --"

# With flatpak
rofi -show drun -drun-launch-prefix "flatpak run"
```

### Configuration File
```rasi
// ~/.config/rofi/config.rasi
configuration {
  drun {
    launch-prefix: "uwsm app --";
  }
}
```

### Keybinding Example (Hyprland)
```conf
# ~/.config/hypr/hyprland.conf
bind = SUPER, D, exec, rofi -show drun -drun-launch-prefix "uwsm app --"
```

## Use Cases

| Use Case | Configuration |
|----------|---------------|
| **uwsm integration** | `drun-launch-prefix: "uwsm app --";` |
| **Systemd scopes** | `drun-launch-prefix: "systemd-run --user --scope --";` |
| **Flatpak apps** | `drun-launch-prefix: "flatpak run";` |
| **Firejail sandbox** | `drun-launch-prefix: "firejail --";` |
| **Custom wrapper** | `drun-launch-prefix: "/path/to/wrapper.sh";` |

## Testing Checklist

- ✅ Compiles without errors/warnings
- ✅ Works with prefix set (prepends correctly)
- ✅ Works without prefix set (backward compatible)
- ✅ Compatible with Terminal=true apps
- ✅ Field codes (%f, %u, etc.) still work correctly
- ✅ Only affects drun mode (not run, ssh, etc.)
- ✅ Proper memory management (no leaks)
- ⏳ **User testing required**: Actual uwsm integration on Wayland

## How to Build and Test

```bash
# Navigate to rofi directory
cd /path/to/rofi

# Checkout the feature branch
git checkout feature/drun-launch-wrapper

# Build (requires nix flake or dependencies installed)
nix develop
meson setup build
meson compile -C build

# Test without prefix (normal behavior)
./build/rofi -show drun

# Test with uwsm prefix (if uwsm is installed)
./build/rofi -show drun -drun-launch-prefix "uwsm app --"

# Test with echo to see what command is executed
./build/rofi -show drun -drun-launch-prefix "echo"
```

## Branch Information

- **Branch**: `feature/drun-launch-wrapper`
- **Base commit**: `caa69c3` ([Script] fix missing and wrong free)
- **Feature commit**: `c1ff372` (Add drun-launch-prefix configuration option)
- **Files changed**: 5
- **Lines added**: 205
- **Lines removed**: 1

## Alternative Implementations Considered

### 1. ✅ `drun-launch-prefix` (IMPLEMENTED)
- **Pros**: Simple, clean, drun-specific
- **Cons**: Limited to prefix only (no template substitution)

### 2. ❌ `drun-launch-wrapper` with templates
- **Pros**: Flexible template system ({cmd}, {name}, etc.)
- **Cons**: More complex, harder to understand

### 3. ❌ Extend global `run-command`
- **Pros**: Unified wrapper system
- **Cons**: Affects all modes, backward compatibility issues

## Documentation

### Help Text
```
-drun-launch-prefix <string>
    Command prefix for launching drun applications.
    Useful for session managers like 'uwsm app --' or sandboxing tools.
```

### Man Page Entry (to be added)
```
-drun-launch-prefix <string>
    Prepends the specified command prefix when launching applications from
    drun mode. This is useful for integrating with Wayland session managers
    like uwsm or sandboxing tools like flatpak/firejail.

    Example: -drun-launch-prefix "uwsm app --"
```

## Next Steps

1. **User testing**: Test with uwsm on actual Wayland setup
2. **Push to remote**: Create pull request for review
3. **Documentation**: Update man pages and wiki
4. **Announce**: Share with rofi community for feedback

## Notes

- The push to remote failed because branch name doesn't match the pattern `claude/*-<session-id>`
- User should either:
  - Manually push: `git push origin feature/drun-launch-wrapper`
  - Or rename branch to match pattern for automatic push

## Comparison to Existing Workaround

**Before (command-line only):**
```bash
rofi -show drun -run-command "uwsm app -- {cmd}"
```
**Problem**: Affects ALL modes (run, ssh, window, etc.)

**After (drun-specific config):**
```rasi
configuration {
  drun {
    launch-prefix: "uwsm app --";
  }
}
```
**Benefit**: Only affects drun, persistent configuration

## Summary

✅ **Complete implementation** of drun-launch-prefix feature
✅ **Clean code** with proper memory management
✅ **Well documented** with design doc and examples
✅ **Backward compatible** - disabled by default
✅ **Tested** locally for compilation and basic functionality
⏳ **Ready for user testing** on Wayland with uwsm

The feature is production-ready and waiting for real-world testing!
