# DRun Launch Wrapper Feature - Design Document

## Problem Statement

When launching applications from rofi's drun mode on Wayland with workspace managers (like Hyprland + uwsm), switching workspaces while an app is launching causes the app to open on the original workspace instead of following the user. This happens because apps are launched as direct child processes instead of through the session manager.

**Current workaround:**
```bash
rofi -show drun -run-command "uwsm app -- {cmd}"
```

**Problems with workaround:**
- Command-line only (not persistent in config)
- Affects ALL modes (run, ssh, etc.), not just drun
- Not discoverable or documented for this use case

## Research Findings

### 1. How UWSM Solves the Problem

**uwsm** (Universal Wayland Session Manager) launches apps as systemd scopes/services:
- Organizes apps in proper systemd slices (app-graphical.slice)
- Prevents apps from being child processes of the compositor
- Allows apps to properly track the active workspace
- Syntax: `uwsm app -- <command>`

### 2. Current Rofi Implementation

**How drun works:**
1. Scans `~/.local/share/applications` and `/usr/share/applications` for `.desktop` files
2. Parses desktop entries using GLib's `GKeyFile`
3. Extracts `Exec=`, `Name=`, `Icon=`, etc.
4. On selection, expands field codes (`%f`, `%u`, etc.) in Exec field
5. Executes via `helper_execute_command()` with optional `run_command` wrapper

**Existing wrapper mechanism:**
- `run_command` config option (default: `"{cmd}"`)
- `run_shell_command` for terminal apps (default: `"{terminal} -e {cmd}"`)
- **Problem**: These are GLOBAL, affecting all modes

### 3. Existing Solutions

**What exists:**
- CLI: `rofi -show drun -run-command "uwsm app -- {cmd}"`
- Global config: `configuration { run-command: "uwsm app -- {cmd}"; }`

**What's missing:**
- Drun-specific wrapper configuration
- Persistent config for uwsm/systemd integration
- Documentation for this use case

## Proposed Solutions

### Option 1: Add `drun-launch-prefix` Configuration (RECOMMENDED)

**Pros:**
- Drun-specific, doesn't affect other modes
- Simple string prefix prepended to command
- Clean, predictable behavior
- Easy to document

**Cons:**
- Less flexible than full template substitution

**Implementation:**
```c
// settings.h
char *drun_launch_prefix;  // Prefix to prepend to drun commands

// xrmoptions.c
{xrm_String, "drun-launch-prefix", {.str = &config.drun_launch_prefix}, NULL,
 "Command prefix for launching drun applications. "
 "Example: 'uwsm app --' or 'systemd-run --user --scope --'",
 CONFIG_DEFAULT}

// config.c
.drun_launch_prefix = NULL,

// drun.c (in exec_cmd_entry, line ~512)
if (config.drun_launch_prefix && strlen(config.drun_launch_prefix) > 0) {
  char *prefixed_cmd = g_strdup_printf("%s %s", config.drun_launch_prefix, fp);
  launched = helper_execute_command(exec_path, prefixed_cmd, terminal, sn ? &context : NULL);
  g_free(prefixed_cmd);
} else {
  launched = helper_execute_command(exec_path, fp, terminal, sn ? &context : NULL);
}
```

**Usage:**
```bash
rofi -show drun -drun-launch-prefix "uwsm app --"
```

Or in config:
```rasi
configuration {
  drun {
    launch-prefix: "uwsm app --";
  }
}
```

### Option 2: Add `drun-launch-wrapper` with Template Substitution

**Pros:**
- Most flexible (supports `{cmd}`, `{name}`, `{exec}`, etc.)
- Allows complex wrappers
- Future-proof for advanced use cases

**Cons:**
- More complex implementation
- Requires template parsing
- Potentially confusing for simple use cases

**Implementation:**
```c
// settings.h
char *drun_launch_wrapper;  // Template for wrapping drun commands

// Example usage templates:
// "uwsm app -- {cmd}"
// "flatpak run {app_id}"
// "systemd-run --user --scope --unit=app-{name} -- {cmd}"
```

### Option 3: Extend `run-command` with Mode Context

**Pros:**
- Single unified wrapper system
- Reuses existing infrastructure

**Cons:**
- More invasive changes to core
- Affects all modes
- Backwards compatibility concerns

## Recommendation: Option 1 (drun-launch-prefix)

### Rationale

1. **Simplicity**: 95% of use cases just need a prefix
2. **Discoverability**: Easy to understand and document
3. **Drun-specific**: Doesn't affect other modes
4. **Minimal changes**: ~30 lines of code across 4 files
5. **Testing**: Easy to verify behavior

### Implementation Plan

**Files to modify:**
1. `include/settings.h` - Add config field
2. `source/xrmoptions.c` - Register option
3. `config/config.c` - Set default
4. `source/modes/drun.c` - Apply prefix in exec_cmd_entry()

**Testing checklist:**
- [ ] Launches apps with uwsm prefix
- [ ] Works with Terminal=true apps
- [ ] Doesn't break without prefix set
- [ ] Works with field codes (%f, %u, etc.)
- [ ] Compiles without errors/warnings
- [ ] Documented in help

### Alternative Implementations for Different Needs

| Use Case | Solution |
|----------|----------|
| uwsm integration | `drun-launch-prefix: "uwsm app --"` |
| Flatpak apps | `drun-launch-prefix: "flatpak run"` |
| Systemd scopes | `drun-launch-prefix: "systemd-run --user --scope --"` |
| Firejail sandbox | `drun-launch-prefix: "firejail --"` |
| Custom script | `drun-launch-prefix: "/path/to/wrapper.sh"` |

## Next Steps

1. Implement Option 1 (drun-launch-prefix)
2. Test with uwsm on Wayland
3. Update documentation
4. Commit and create feature branch
