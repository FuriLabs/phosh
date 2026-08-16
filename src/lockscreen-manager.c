/*
 * Copyright (C) 2018 Purism SPC
 *               2024-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-lockscreen-manager"

#include "background-cache.h"
#include "background-image.h"
#include "lockscreen-manager-priv.h"
#include "lockscreen-priv.h"
#include "lockshield.h"
#include "monitor-manager.h"
#include "monitor/monitor.h"
#include "phosh-wayland.h"
#include "shell-priv.h"
#include "util.h"

#include <gmobile.h>
#include <gdesktop-enums.h>
#include <gdk/gdkwayland.h>

#define SCREENSAVER_SETTINGS "org.gnome.desktop.screensaver"
#define KEY_PICTURE_URI       "picture-uri"
#define KEY_PICTURE_OPTIONS   "picture-options"

/**
 * PhoshLockscreenManager:
 *
 * The singleton that manages screen locking
 *
 * The #PhoshLockscreenManager is responsible for putting the #PhoshLockscreen
 * on the primary output and a #PhoshLockshield on other outputs when the session
 * becomes idle or when invoked explicitly via phosh_lockscreen_manager_set_locked().
 */

enum {
  WAKEUP_OUTPUTS,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

enum {
  PROP_0,
  PROP_LOCKED,
  PROP_CALLS_MANAGER,
  PROP_KIOSK_MODE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


struct _PhoshLockscreenManager {
  GObject                  parent;

  PhoshLockscreen         *lockscreen;     /* phone display lock screen */
  GPtrArray               *shields;        /* other outputs */

  GSettings               *bg_settings;
  GSettings               *shell_settings;  /* io.furios.phosh.shell, may be NULL */
  GSettings               *desktop_bg_settings;
  GFile                   *bg_file;
  GFileMonitor            *bg_file_monitor;
  GDesktopBackgroundStyle  bg_style;
  PhoshBackgroundImage    *cached_bg_image;
  GCancellable            *bg_load_cancel;

  gboolean                 locked;
  gboolean                 locking;
  guint                    watch_id;
  gboolean                 blanked;
  gint64                   active_time;    /* when lock was activated (in us) */

  PhoshCallsManager       *calls_manager;  /* Calls DBus Interface */

  gboolean                 kiosk_mode;
};

G_DEFINE_TYPE (PhoshLockscreenManager, phosh_lockscreen_manager, G_TYPE_OBJECT)


static void
on_background_cache_fetch_ready (GObject *source_object, GAsyncResult *res, gpointer data)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (data);
  PhoshBackgroundCache *cache = PHOSH_BACKGROUND_CACHE (source_object);
  g_autoptr (PhoshBackgroundImage) image = NULL;
  g_autoptr (GError) err = NULL;

  image = phosh_background_cache_fetch_finish (cache, res, &err);
  if (!image) {
    phosh_async_error_warn (err, "Failed to load background image");
    return;
  }

  g_assert (PHOSH_IS_LOCKSCREEN_MANAGER (self));
  g_assert (PHOSH_IS_BACKGROUND_CACHE (cache));

  g_set_object (&self->cached_bg_image, image);
  if (self->lockscreen)
    phosh_lockscreen_set_bg_image (self->lockscreen, self->cached_bg_image);
}


static void
load_background (PhoshLockscreenManager *self)
{
  PhoshBackgroundCache *cache = phosh_background_cache_get_default ();

  g_cancellable_cancel (self->bg_load_cancel);
  g_clear_object (&self->bg_load_cancel);
  self->bg_load_cancel = g_cancellable_new ();

  phosh_background_cache_fetch_async (cache,
                                      self->bg_file,
                                      self->bg_load_cancel,
                                      on_background_cache_fetch_ready,
                                      self);
}


static void
on_file_changed (PhoshLockscreenManager *self,
                 GFile                  *file,
                 GFile                  *other_file,
                 GFileMonitorEvent       event_type,
                 GFileMonitor           *monitor)
{
  PhoshBackgroundCache *cache = phosh_background_cache_get_default ();

  if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    return;

  g_debug ("Lockscreen bg file %s changed, clearing cache", g_file_peek_path (self->bg_file));
  phosh_background_cache_remove (cache, self->bg_file);

  load_background (self);
}


static void
monitor_bg_file (PhoshLockscreenManager *self)
{
  g_autoptr (GError) err = NULL;

  g_clear_object (&self->bg_file_monitor);

  self->bg_file_monitor = g_file_monitor_file (self->bg_file, G_FILE_MONITOR_NONE, NULL, &err);
  if (!self->bg_file_monitor) {
    g_autofree char *uri = g_file_get_uri (self->bg_file);

    g_warning ("Failed to setup file monitor for %s: %s", uri, err->message);
    return;
  }

  g_signal_connect_object (self->bg_file_monitor,
                           "changed",
                           G_CALLBACK (on_file_changed),
                           self,
                           G_CONNECT_SWAPPED);
}


static void
on_picture_params_changed (PhoshLockscreenManager *self)
{
  PhoshBackgroundCache *cache = phosh_background_cache_get_default ();
  GDesktopBackgroundStyle style;
  g_autoptr (GFile) file = NULL;
  g_autofree char *uri = NULL;

  uri = g_settings_get_string (self->bg_settings, KEY_PICTURE_URI);
  style = g_settings_get_enum (self->bg_settings, KEY_PICTURE_OPTIONS);

  if (!gm_str_is_null_or_empty (uri) &&
      g_str_has_prefix (uri, "file:///") &&
      style != G_DESKTOP_BACKGROUND_STYLE_NONE) {
    file = g_file_new_for_uri (uri);
  }

  /* The style can change on its own, with the same picture. Pushing it before
   * the file comparison below matters: that comparison returns early when only
   * the style moved, which is why picture-options had no effect on the lock
   * screen at all. */
  if (style != self->bg_style) {
    self->bg_style = style;
    if (self->lockscreen)
      phosh_lockscreen_set_bg_style (self->lockscreen, style);
  }

  if (phosh_util_file_equal (self->bg_file, file))
    return;

  if (self->bg_file)
    phosh_background_cache_remove (cache, self->bg_file);
  g_clear_object (&self->bg_file);
  g_clear_object (&self->bg_file_monitor);
  g_clear_object (&self->cached_bg_image);

  if (!file)
    return;

  g_set_object (&self->bg_file, file);
  g_debug ("Loading '%s'", g_file_peek_path (self->bg_file));
  monitor_bg_file (self);
  load_background (self);
}


static void
on_lockscreen_unlock (PhoshLockscreenManager *self, PhoshLockscreen *lockscreen)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitorManager *monitor_manager = phosh_shell_get_monitor_manager (shell);
  PhoshMonitor *primary_monitor = phosh_shell_get_primary_monitor (shell);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (lockscreen));
  g_return_if_fail (lockscreen == PHOSH_LOCKSCREEN (self->lockscreen));

  g_signal_handlers_disconnect_by_data (monitor_manager, self);
  g_signal_handlers_disconnect_by_data (primary_monitor, self);
  g_signal_handlers_disconnect_by_data (shell, self);
  g_clear_pointer (&self->lockscreen, phosh_cp_widget_destroy);

  /* Unlock all other outputs */
  g_clear_pointer (&self->shields, g_ptr_array_unref);

  self->locked = FALSE;
  self->active_time = 0;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LOCKED]);
}


static void
on_lockscreen_wakeup_output (PhoshLockscreenManager *self, PhoshLockscreen *lockscreen)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (lockscreen));

  /* we just proxy the signal here */
  g_signal_emit (self, signals[WAKEUP_OUTPUTS], 0);
}


/* Lock a non primary monitor bringing up a shield */
static void
lock_monitor (PhoshLockscreenManager *self,
              PhoshMonitor           *monitor)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshWayland *wl = phosh_wayland_get_default ();
  GtkWidget *shield;

  /* Primary monitor is handled via on_primary_monitor_changed */
  if (phosh_shell_get_primary_monitor (shell) == monitor)
    return;

  g_debug ("Adding shield for %s", monitor->name);
  shield = phosh_lockshield_new (phosh_wayland_get_zwlr_layer_shell_v1 (wl), monitor);

  g_object_set_data (G_OBJECT (shield), "phosh-monitor", monitor);

  g_ptr_array_add (self->shields, shield);
  gtk_widget_set_visible (shield, TRUE);
}


static void
remove_shield_by_monitor (PhoshLockscreenManager *self,
                          PhoshMonitor           *monitor)
{
  if (!self->shields) {
    g_debug ("Shields already gone");
    return;
  }

  for (int i = 0; i < self->shields->len; i++) {
    PhoshMonitor *shield_monitor;
    PhoshLockshield *shield = g_ptr_array_index (self->shields, i);

    shield_monitor = g_object_get_data (G_OBJECT (shield), "phosh-monitor");
    g_return_if_fail (PHOSH_IS_MONITOR (shield_monitor));
    if (shield_monitor == monitor) {
      g_debug ("Removing shield %p", shield);
      g_ptr_array_remove (self->shields, shield);
      break;
    }
  }
}


static void
on_monitor_removed (PhoshLockscreenManager *self,
                    PhoshMonitor           *monitor,
                    PhoshMonitorManager    *monitormanager)
{


  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  g_debug ("Monitor '%s' removed", monitor->name);
  remove_shield_by_monitor (self, monitor);
}


static void
on_monitor_added (PhoshLockscreenManager *self,
                  PhoshMonitor           *monitor,
                  PhoshMonitorManager    *monitormanager)
{
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  g_debug ("Monitor '%s' added", monitor->name);
  lock_monitor (self, monitor);
}


static void
lock_primary_monitor (PhoshLockscreenManager *self)
{
  GType lockscreen_type;
  PhoshMonitor *primary_monitor;
  PhoshWayland *wl = phosh_wayland_get_default ();
  PhoshShell *shell = phosh_shell_get_default ();

  lockscreen_type = phosh_shell_get_lockscreen_type (shell);
  primary_monitor = phosh_shell_get_primary_monitor (shell);
  g_assert (PHOSH_IS_MONITOR (primary_monitor));

  /* The primary output gets the clock, keypad, ... */
  self->lockscreen = PHOSH_LOCKSCREEN (phosh_lockscreen_new (
                                         lockscreen_type,
                                         phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                                         primary_monitor->wl_output,
                                         self->calls_manager));
  g_object_connect (self->lockscreen,
                    "swapped-object-signal::lockscreen-unlock", on_lockscreen_unlock, self,
                    "swapped-object-signal::wakeup-output", on_lockscreen_wakeup_output, self,
                    NULL);
  phosh_lockscreen_set_bg_style (self->lockscreen, self->bg_style);
  phosh_lockscreen_set_bg_image (self->lockscreen, self->cached_bg_image);

  gtk_widget_set_visible (GTK_WIDGET (self->lockscreen), TRUE);
  /* Old lockscreen gets remove due to `layer_surface_closed` */
}


static void
on_primary_monitor_changed (PhoshLockscreenManager *self,
                            GParamSpec             *pspec,
                            PhoshShell             *shell)
{
  PhoshMonitor *monitor;

  g_return_if_fail (PHOSH_IS_SHELL (shell));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  monitor = phosh_shell_get_primary_monitor (shell);

  if (monitor) {
    g_debug ("primary monitor changed to %s, need to move lockscreen", monitor->name);
    lock_primary_monitor (self);
    /* We don't remove a shield that might exist to avoid the screen
       content flickering in. The shield will be removed on unlock */
  } else {
    g_debug ("Primary monitor gone, doing nothing");
  }
}


#define DESKTOP_BG_SCHEMA_ID "org.gnome.desktop.background"
#define DESKTOP_BG_KEY_URI      "picture-uri"
#define DESKTOP_BG_KEY_URI_DARK "picture-uri-dark"

/* The FuriOS schema is phosh's own, so the key is simply added to it. */
#define PHOSH_FURIOS_SCHEMA_ID     "io.furios.phosh.shell"
#define PHOSH_KEY_WALLPAPER_FOLDER "wallpaper-folder"


static gboolean
is_image_name (const char *name)
{
  static const char * const exts[] = { ".jpg", ".jpeg", ".png", ".webp", ".jxl", ".bmp", ".svg" };
  g_autofree char *lower = g_ascii_strdown (name, -1);

  for (guint i = 0; i < G_N_ELEMENTS (exts); i++) {
    if (g_str_has_suffix (lower, exts[i]))
      return TRUE;
  }

  return FALSE;
}

/**
 * rotate_wallpaper:
 * @self: The #PhoshLockscreenManager
 *
 * Move the wallpaper on to the next image in the configured folder.
 *
 * Called whenever the lock screen becomes visible: when the phone locks, and
 * when the screen is woken while it is still locked. Deliberately not on
 * unlock, so that what you unlock into is the wallpaper you were just looking
 * at rather than one you never see.
 */
static void
rotate_wallpaper (PhoshLockscreenManager *self)
{
  g_autofree char *folder = NULL;
  g_autofree char *current = NULL;
  g_autoptr (GDir) dir = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (GPtrArray) names = NULL;
  g_autofree char *path = NULL;
  g_autofree char *uri = NULL;
  const char *name;
  guint next = 0;

  if (self->shell_settings == NULL)
    return;

  folder = g_settings_get_string (self->shell_settings, PHOSH_KEY_WALLPAPER_FOLDER);
  if (gm_str_is_null_or_empty (folder))
    return;

  dir = g_dir_open (folder, 0, &err);
  if (dir == NULL) {
    g_warning ("Cannot rotate the wallpaper through '%s': %s", folder, err->message);
    return;
  }

  names = g_ptr_array_new_with_free_func (g_free);
  while ((name = g_dir_read_name (dir))) {
    if (is_image_name (name))
      g_ptr_array_add (names, g_strdup (name));
  }

  if (names->len == 0) {
    g_debug ("No images in '%s' to rotate through", folder);
    return;
  }

  /* Filename order, so the sequence is predictable and stable */
  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);

  /* Take the one after whatever is in use. An unrecognised wallpaper -- one
   * from outside the folder -- starts the sequence from the beginning. */
  current = g_settings_get_string (self->desktop_bg_settings, DESKTOP_BG_KEY_URI);
  if (!gm_str_is_null_or_empty (current)) {
    g_autoptr (GFile) file = g_file_new_for_uri (current);
    g_autofree char *base = g_file_get_basename (file);

    for (guint i = 0; i < names->len; i++) {
      if (g_strcmp0 (g_ptr_array_index (names, i), base) == 0) {
        next = (i + 1) % names->len;
        break;
      }
    }
  }

  path = g_build_filename (folder, g_ptr_array_index (names, next), NULL);
  uri = g_filename_to_uri (path, NULL, &err);
  if (uri == NULL) {
    g_warning ("Cannot build a URI for '%s': %s", path, err->message);
    return;
  }

  g_debug ("Rotating wallpaper to %s", uri);
  /* Both colour schemes, so the rotation shows whichever one is in use */
  g_settings_set_string (self->desktop_bg_settings, DESKTOP_BG_KEY_URI, uri);
  g_settings_set_string (self->desktop_bg_settings, DESKTOP_BG_KEY_URI_DARK, uri);
  /* And the lock screen, which has a wallpaper of its own in the screensaver
   * schema rather than sharing the desktop's. Without this the rotation is
   * invisible in the one place it was asked for. */
  g_settings_set_string (self->bg_settings, KEY_PICTURE_URI, uri);
}


static void on_power_mode_changed     (PhoshLockscreenManager *self,
                                       GParamSpec             *pspec,
                                       PhoshMonitor           *monitor);
static void on_shell_state_changed    (PhoshLockscreenManager *self,
                                       GParamSpec             *pspec,
                                       PhoshShell             *shell);
static void on_primary_monitor_swapped (PhoshLockscreenManager *self,
                                        GParamSpec             *pspec,
                                        PhoshShell             *shell);

/*
 * Start watching the primary monitor for the screen turning on and off.
 *
 * From a timeout rather than from init(): the shell singleton is still being
 * built while the manager is constructed, and so is its primary monitor, so
 * this keeps trying until both exist. Giving up after one attempt would leave
 * the screen unwatched for the rest of the session, with only locking left to
 * rotate the wallpaper.
 */
static gboolean
watch_power_mode (gpointer data)
{
  PhoshLockscreenManager *self = data;
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitor *monitor;
  PhoshMonitorPowerSaveMode mode;

  if (shell == NULL)
    return G_SOURCE_CONTINUE;

  monitor = phosh_shell_get_primary_monitor (shell);
  if (monitor == NULL)
    return G_SOURCE_CONTINUE;

  self->watch_id = 0;

  g_object_get (monitor, "power-mode", &mode, NULL);
  self->blanked = (mode == PHOSH_MONITOR_POWER_SAVE_MODE_OFF);

  g_signal_connect_object (monitor,
                           "notify::power-mode",
                           G_CALLBACK (on_power_mode_changed),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (shell,
                           "notify::shell-state",
                           G_CALLBACK (on_shell_state_changed),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (shell,
                           "notify::primary-monitor",
                           G_CALLBACK (on_primary_monitor_swapped),
                           self,
                           G_CONNECT_SWAPPED);

  return G_SOURCE_REMOVE;
}


/*
 * Both sources feed this, and either one is enough.
 *
 * The shell's PHOSH_STATE_BLANKED survives the primary monitor being replaced,
 * which happens on output changes and leaves a signal attached to the old
 * monitor object firing never again. The monitor's own power-mode is the more
 * direct signal when it is there, but goes away with the monitor. Either one
 * alone therefore has a blind spot: watch both and let the first to notice
 * win -- the edge check below means a duplicate costs nothing.
 */
static void
set_blanked (PhoshLockscreenManager *self, gboolean blanked)
{
  g_assert (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  /* The monitor's own power mode, rather than the shell's PHOSH_STATE_BLANKED.
   * That flag lives in a flags property which notifies on any change in it, so
   * it needs edge detection -- and a single missed blank leaves the remembered
   * value stuck at "awake" for the rest of the session, after which no wake is
   * ever seen as a transition and the wallpaper silently stops rotating until
   * the shell restarts. Watching the monitor removes the guesswork: this
   * signal fires only when the screen actually turns on or off. */
  if (blanked == self->blanked)
    return;

  self->blanked = blanked;

  /* Any wake, with no further condition.
   *
   * Not conditional on being locked, nor on a lock screen existing: pressing
   * the power button blanks the screen without locking it, and
   * org.gnome.desktop.screensaver lock-delay is 1800s by default, so either
   * condition would advance the wallpaper only after half an hour asleep.
   *
   * Waking the phone is the moment the wallpaper is looked at again, whether
   * it is the lock screen or the home screen that appears, so that is the
   * trigger. */
  if (blanked)
    return;

  rotate_wallpaper (self);
}


static void
on_power_mode_changed (PhoshLockscreenManager *self, GParamSpec *pspec, PhoshMonitor *monitor)
{
  PhoshMonitorPowerSaveMode mode;

  g_object_get (monitor, "power-mode", &mode, NULL);
  set_blanked (self, mode == PHOSH_MONITOR_POWER_SAVE_MODE_OFF);
}


static void
on_shell_state_changed (PhoshLockscreenManager *self, GParamSpec *pspec, PhoshShell *shell)
{
  set_blanked (self, !!(phosh_shell_get_state (shell) & PHOSH_STATE_BLANKED));
}


static void
on_primary_monitor_swapped (PhoshLockscreenManager *self, GParamSpec *pspec, PhoshShell *shell)
{
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (shell);

  /* A new monitor object needs its own connection; the old one is gone */
  if (monitor == NULL)
    return;

  g_signal_connect_object (monitor, "notify::power-mode",
                           G_CALLBACK (on_power_mode_changed), self, G_CONNECT_SWAPPED);
}


static void
lockscreen_lock (PhoshLockscreenManager *self)
{
  PhoshMonitor *primary_monitor;
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitorManager *monitor_manager = phosh_shell_get_monitor_manager (shell);

  g_return_if_fail (!self->locked);

  /* Locking already in progress */
  if (self->locking)
    return;

  self->locking = TRUE;
  primary_monitor = phosh_shell_get_primary_monitor (shell);

  /* A fresh wallpaper for the lock screen that is about to appear */
  rotate_wallpaper (self);

  /* Listen for monitor changes */
  g_signal_connect_object (monitor_manager, "monitor-added",
                           G_CALLBACK (on_monitor_added),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (monitor_manager, "monitor-removed",
                           G_CALLBACK (on_monitor_removed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (shell,
                           "notify::primary-monitor",
                           G_CALLBACK (on_primary_monitor_changed),
                           self,
                           G_CONNECT_SWAPPED);

  if (primary_monitor)
    lock_primary_monitor (self);
  else
    g_message ("No primary monitor to lock");

  /* Lock all other outputs */
  self->shields = g_ptr_array_new_with_free_func ((GDestroyNotify) (gtk_widget_destroy));
  for (int i = 0; i < phosh_monitor_manager_get_num_monitors (monitor_manager); i++) {
    PhoshMonitor *monitor = phosh_monitor_manager_get_monitor (monitor_manager, i);

    if (monitor == NULL || monitor == primary_monitor)
      continue;
    lock_monitor (self, monitor);
  }

  self->locked = TRUE;
  self->locking = FALSE;
  self->active_time = g_get_monotonic_time ();
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LOCKED]);
}


static void
phosh_lockscreen_manager_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);

  switch (property_id) {
  case PROP_LOCKED:
    phosh_lockscreen_manager_set_locked (self, g_value_get_boolean (value));
    break;
  case PROP_CALLS_MANAGER:
    self->calls_manager = g_value_dup_object (value);
    break;
  case PROP_KIOSK_MODE:
    self->kiosk_mode = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_lockscreen_manager_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);

  switch (property_id) {
  case PROP_LOCKED:
    g_value_set_boolean (value, self->locked);
    break;
  case PROP_CALLS_MANAGER:
    g_value_set_object (value, self->calls_manager);
    break;
  case PROP_KIOSK_MODE:
    g_value_set_boolean (value, self->kiosk_mode);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_calls_call_added (PhoshLockscreen *self)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  g_signal_emit (self, signals[WAKEUP_OUTPUTS], 0);
}


static void
phosh_lockscreen_manager_dispose (GObject *object)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);

  g_clear_pointer (&self->shields, g_ptr_array_unref);
  g_clear_pointer (&self->lockscreen, phosh_cp_widget_destroy);
  g_clear_object (&self->calls_manager);

  g_cancellable_cancel (self->bg_load_cancel);
  g_clear_object (&self->bg_load_cancel);

  g_clear_object (&self->bg_file_monitor);
  g_clear_object (&self->bg_file);
  g_clear_handle_id (&self->watch_id, g_source_remove);
  g_clear_object (&self->bg_settings);
  g_clear_object (&self->desktop_bg_settings);
  g_clear_object (&self->shell_settings);
  g_clear_object (&self->cached_bg_image);

  G_OBJECT_CLASS (phosh_lockscreen_manager_parent_class)->dispose (object);
}


static void
phosh_lockscreen_manager_constructed (GObject *object)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);

  G_OBJECT_CLASS (phosh_lockscreen_manager_parent_class)->constructed (object);

  g_signal_connect_object (self->calls_manager,
                           "call-added",
                           G_CALLBACK (on_calls_call_added),
                           self,
                           G_CONNECT_SWAPPED);
}


static void
phosh_lockscreen_manager_class_init (PhoshLockscreenManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_lockscreen_manager_constructed;
  object_class->dispose = phosh_lockscreen_manager_dispose;

  object_class->set_property = phosh_lockscreen_manager_set_property;
  object_class->get_property = phosh_lockscreen_manager_get_property;

  /**
   * PhoshLockscreenManager:locked:
   *
   * Whether the screen is locked
   */
  props[PROP_LOCKED] =
    g_param_spec_boolean ("locked", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_CALLS_MANAGER] =
    g_param_spec_object ("calls-manager", "", "",
                         PHOSH_TYPE_CALLS_MANAGER,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_KIOSK_MODE] =
    g_param_spec_boolean ("kiosk-mode", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshLockscreenManager::wakeup-outputs
   * @self: The #PhoshLockscreenManager emitting this signal
   *
   * Emitted when the outputs should be woken up.
   */
  signals[WAKEUP_OUTPUTS] = g_signal_new (
    "wakeup-outputs",
    G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
    NULL, G_TYPE_NONE, 0);
}


static void
phosh_lockscreen_manager_init (PhoshLockscreenManager *self)
{
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  g_autoptr (GSettingsSchema) schema = NULL;

  self->watch_id = g_timeout_add (1000, watch_power_mode, self);

  self->bg_settings = g_settings_new (SCREENSAVER_SETTINGS);
  self->desktop_bg_settings = g_settings_new (DESKTOP_BG_SCHEMA_ID);

  /* Wallpaper rotation is optional: the schema belongs to this fork, so check
   * it is installed and carries the key before touching it. Asking GSettings
   * for a key a schema does not have aborts the process. */
  if (source)
    schema = g_settings_schema_source_lookup (source, PHOSH_FURIOS_SCHEMA_ID, TRUE);

  if (schema && g_settings_schema_has_key (schema, PHOSH_KEY_WALLPAPER_FOLDER))
    self->shell_settings = g_settings_new (PHOSH_FURIOS_SCHEMA_ID);

  g_object_connect (self->bg_settings,
                    "swapped-object-signal::changed::" KEY_PICTURE_URI,
                    G_CALLBACK (on_picture_params_changed),
                    self,
                    "swapped-object-signal::changed::" KEY_PICTURE_OPTIONS,
                    G_CALLBACK (on_picture_params_changed),
                    self,
                    NULL);
  on_picture_params_changed (self);
}


PhoshLockscreenManager *
phosh_lockscreen_manager_new (PhoshCallsManager *calls_manager,
                              gboolean           kiosk_mode)
{
  return g_object_new (PHOSH_TYPE_LOCKSCREEN_MANAGER,
                       "calls-manager", calls_manager,
                       "kiosk-mode", kiosk_mode,
                       NULL);
}

/**
 * phosh_lockscreen_set_locked:
 * @self: The #PhoshLockscreenManager
 * @lock: %TRUE to lock %FALSE to unlock
 *
 * Lock or unlock the screen.
 */
void
phosh_lockscreen_manager_set_locked (PhoshLockscreenManager *self, gboolean lock)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));
  lock = lock && !self->kiosk_mode;
  if (lock == self->locked)
    return;

  if (lock)
    lockscreen_lock (self);
  else
    on_lockscreen_unlock (self, PHOSH_LOCKSCREEN (self->lockscreen));
}


gboolean
phosh_lockscreen_manager_get_locked (PhoshLockscreenManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), FALSE);

  return self->locked;
}

/**
 * phosh_lockscreen_manager_get_page
 * @self: The #PhoshLockscreenManager
 *
 * Returns: The currently shown #PhoshLockscreenPage in the #PhoshLockscreen
 */
PhoshLockscreenPage
phosh_lockscreen_manager_get_page (PhoshLockscreenManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), FALSE);

  return phosh_lockscreen_get_page (self->lockscreen);
}


gint64
phosh_lockscreen_manager_get_active_time (PhoshLockscreenManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), 0);

  return self->active_time;
}


gboolean
phosh_lockscreen_manager_set_page  (PhoshLockscreenManager *self,
                                    PhoshLockscreenPage     page)
{
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), FALSE);

  if (!self->lockscreen)
    return FALSE;

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN (self->lockscreen), FALSE);

  phosh_lockscreen_set_page (self->lockscreen, page);
  return TRUE;
}


/**
 * phosh_lockscreen_manager_get_lockscreen:
 * @self: The lockscreen manager
 *
 * Gets the current [type@Lockscreen], if one exists (NULL otherwise).
 *
 * Returns:(transfer none)(nullable): The lockscreen
 */
PhoshLockscreen*
phosh_lockscreen_manager_get_lockscreen (PhoshLockscreenManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), NULL);

  if (!self->lockscreen)
    return NULL;

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN (self->lockscreen), FALSE);

  return self->lockscreen;
}
