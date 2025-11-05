# Daemon Mode Implementation Plan - Low Latency Window Management

## Goal
Make rofi daemon mode fully functional with **minimal latency** - users should be able to start typing immediately without waiting for visual confirmation.

## Performance Target
- Window appears: < 50ms
- Ready for input: < 30ms
- This requires keeping display/surface alive between invocations

## Architecture Overview

### What to Keep Alive (Persist Between Invocations)
✅ Already persisting:
- Main loop (`main_loop`)
- Display connection (`wayland->display` or `xcb->connection`)
- Socket/daemon infrastructure
- Mode configurations

❌ Currently destroyed, needs to persist:
- **Surface** (`wl_surface`, `zwlr_layer_surface_v1`)
- **Layer shell surface** (critical for Wayland)
- Output/monitor bindings
- Compositor protocol objects

### What to Recreate Per Invocation
- `RofiViewState` (view state)
- Widget tree (buttons, textbox, listview)
- Mode-specific state
- Input buffer content

## Implementation Plan

### Phase 1: Add "Hide Without Destroy" Support

#### 1.1 Create New Display Layer Functions
**File**: `include/display-internal.h`

Add new optional function pointer to `display_proxy`:
```c
typedef struct {
  // ... existing functions ...

  // New: Hide surface without destroying it (for daemon mode)
  void (*hide_surface_keep_alive)(void);

  // New: Show previously hidden surface (for daemon mode)
  gboolean (*show_surface_reuse)(void);

} display_proxy;
```

#### 1.2 Implement Wayland Version
**File**: `source/wayland/display.c`

Add daemon-mode-aware hide:
```c
static void wayland_display_hide_surface_keep_alive(void) {
  if (wayland->wlr_surface == NULL) {
    return;
  }

  // Option 1: Set surface to 0x0 size (hides but keeps alive)
  zwlr_layer_surface_v1_set_size(wayland->wlr_surface, 0, 0);
  wl_surface_commit(wayland->surface);

  // Option 2: If layer shell v4+, use visibility
  // zwlr_layer_surface_v1_set_keyboard_interactivity(wayland->wlr_surface, 0);

  wl_display_flush(wayland->display);

  // Important: Do NOT call wayland_surface_destroy()
}

static gboolean wayland_display_show_surface_reuse(void) {
  if (wayland->wlr_surface == NULL) {
    return FALSE; // Surface was destroyed, need full recreate
  }

  // Surface still exists, just needs to be shown again
  // Size will be set by rofi_view_window_update_size()
  // when creating the new view

  return TRUE;
}
```

Register in `display_` struct:
```c
static display_proxy display_ = {
  // ... existing ...
  .hide_surface_keep_alive = wayland_display_hide_surface_keep_alive,
  .show_surface_reuse = wayland_display_show_surface_reuse,
};
```

#### 1.3 Implement XCB Version
**File**: `source/xcb/display.c`

```c
static void xcb_display_hide_surface_keep_alive(void) {
  if (CacheState.main_window == XCB_WINDOW_NONE) {
    return;
  }

  display_revert_input_focus();
  xcb_unmap_window(xcb->connection, CacheState.main_window);
  xcb_flush(xcb->connection);

  // Important: Do NOT call xcb_destroy_window()
}

static gboolean xcb_display_show_surface_reuse(void) {
  if (CacheState.main_window == XCB_WINDOW_NONE) {
    return FALSE;
  }

  // Window still exists, just map it
  xcb_map_window(xcb->connection, CacheState.main_window);
  xcb_flush(xcb->connection);

  return TRUE;
}
```

#### 1.4 Add Display API Wrappers
**File**: `source/display.c`

```c
void display_hide_surface_keep_alive(void) {
  if (proxy->hide_surface_keep_alive) {
    proxy->hide_surface_keep_alive();
  } else {
    // Fallback to old behavior
    proxy->early_cleanup();
  }
}

gboolean display_show_surface_reuse(void) {
  if (proxy->show_surface_reuse) {
    return proxy->show_surface_reuse();
  }
  return FALSE;
}
```

**File**: `include/display.h`
```c
void display_hide_surface_keep_alive(void);
gboolean display_show_surface_reuse(void);
```

### Phase 2: Modify View Cleanup for Daemon Mode

#### 2.1 Update process_result()
**File**: `source/rofi.c`

```c
void process_result(RofiViewState *state) {
  // ... existing code ...

  if (mode != MODE_EXIT) {
    rofi_view_switch_mode(state, modes[mode]);
    curr_mode = mode;
    return;
  }

  // On exit, free current view
  if (daemon_mode) {
    // Daemon mode: Hide surface but keep it alive for next use
    display_hide_surface_keep_alive();
  }

  rofi_view_remove_active(state);
  rofi_view_free(state);

  if (daemon_mode) {
    daemon_busy = FALSE;
  }
  return;
}
```

### Phase 3: Optimize View Creation for Daemon Mode

#### 3.1 Fast Path for Surface Reuse
**File**: `source/rofi.c` in `run_mode_index()`

```c
static void run_mode_index(ModeMode mode) {
  // Initialize modes
  for (unsigned int i = 0; i < num_modes; i++) {
    if (!mode_init(modes[i])) {
      // ... error handling ...
    }
  }

  if (rofi_view_get_active() != NULL) {
    return;
  }

  // Daemon mode optimization: Try to reuse existing surface
  if (daemon_mode && display_show_surface_reuse()) {
    g_debug("Daemon: Reusing existing surface (fast path)");
    // Surface is ready, just create new view on top of it
  } else {
    g_debug("Daemon: Creating new surface (slow path)");
    // Surface doesn't exist, will be created in rofi_view_create()
  }

  curr_mode = mode;
  RofiViewState *state = rofi_view_create(modes[mode], config.filter, 0, process_result);

  // ... rest of function ...
}
```

### Phase 4: Widget Optimization (Optional - Phase 2 work)

For even lower latency, consider widget pooling:

**File**: `source/view.c`

```c
// Global widget cache for daemon mode
static struct {
  widget *cached_main_window;
  widget *cached_input_bar;
  widget *cached_listview;
  gboolean valid;
} daemon_widget_cache = {0};

void rofi_view_create(...) {
  if (daemon_mode && daemon_widget_cache.valid) {
    // Reuse cached widgets, just update content
    state->main_window = daemon_widget_cache.cached_main_window;
    // Reset widget state, update mode-specific content
  } else {
    // Create fresh widgets
    state->main_window = widget_create(...);

    if (daemon_mode) {
      // Cache for next time
      daemon_widget_cache.cached_main_window = state->main_window;
      daemon_widget_cache.valid = TRUE;
    }
  }
}
```

## Implementation Steps (Ordered)

### Step 1: Display Layer Changes
1. ✅ Add `hide_surface_keep_alive` to display_proxy
2. ✅ Add `show_surface_reuse` to display_proxy
3. ✅ Implement Wayland version (test on Wayland first)
4. ✅ Implement XCB version
5. ✅ Add display.c wrappers

### Step 2: Daemon Mode Integration
1. ✅ Update `process_result()` to use new hide function
2. ✅ Update `run_mode_index()` to try surface reuse
3. ✅ Test: Daemon stays alive, no crashes
4. ✅ Test: Window hides properly after use
5. ✅ Test: Window appears quickly on second invoke

### Step 3: Testing & Optimization
1. ✅ Measure latency (time from request to window visible)
2. ✅ Test rapid invocations (super+m spam)
3. ✅ Test different modes (drun, window, run)
4. ✅ Test with dmenu mode (should still not forward)
5. ✅ Profile: Identify remaining bottlenecks

### Step 4: Optional Enhancements
1. Widget pooling (if latency still too high)
2. Pre-fetch/cache mode data in background
3. Compositor hints for faster surface mapping

## Testing Checklist

```bash
# Start daemon
rofi -daemon &

# Test 1: Basic functionality
rofi -show drun
# Select app - window should hide, daemon alive

# Test 2: Rapid invocations
rofi -show drun &
rofi -show drun &  # Should reject, not crash

# Test 3: Latency test
time rofi -show drun
# Should be < 50ms on second+ invoke

# Test 4: Multiple modes
rofi -show window
rofi -show run
rofi -show drun

# Test 5: dmenu still works
echo "a\nb\nc" | rofi -dmenu

# Test 6: Cleanup on exit
ps aux | grep rofi  # Should show daemon
kill $(cat /run/user/$UID/rofi.pid)
ls /run/user/$UID/rofi*  # Should be cleaned up
```

## Success Criteria

- ✅ Daemon doesn't crash on repeated use
- ✅ Window hides after selection (not blocking input)
- ✅ Window appears in < 50ms on subsequent invocations
- ✅ User can start typing immediately (no wait for visual confirm)
- ✅ Daemon cleans up properly on exit
- ✅ dmenu mode works without daemon
- ✅ No memory leaks over extended use

## Files to Modify

1. `include/display-internal.h` - Add function pointers
2. `include/display.h` - Add public API
3. `source/display.c` - Add wrapper functions
4. `source/wayland/display.c` - Implement Wayland version
5. `source/xcb/display.c` - Implement XCB version
6. `source/rofi.c` - Use new functions in daemon mode
7. Optional: `source/view.c` - Widget caching

## Risk Analysis

### Low Risk
- Adding new optional functions to display_proxy
- Wayland hide via size 0x0 (standard technique)
- XCB unmap (well-tested)

### Medium Risk
- Surface lifecycle management (might leak if not careful)
- Widget reuse (state contamination between modes)

### Mitigation
- Add DEBUG logging for surface lifecycle events
- Valgrind testing for memory leaks
- Extensive testing across modes
- Fallback to slow path if surface reuse fails

## Timeline Estimate

- Phase 1 (Display Layer): 2-3 hours
- Phase 2 (Integration): 1-2 hours
- Phase 3 (Testing): 1-2 hours
- **Total**: 4-7 hours for basic working version
- Phase 4 (Optimization): +2-3 hours if needed

## Notes

- Start with Wayland since that's what you're using
- XCB version can come later or be left as "not supported in daemon mode on X11" if needed
- Widget caching is optional optimization, not required for basic functionality
- Focus on correctness first, then optimize if latency isn't good enough
