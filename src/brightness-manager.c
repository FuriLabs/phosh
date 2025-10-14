/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-brightness-manager"

#include "phosh-config.h"

#include "brightness-manager.h"
#include "shell-priv.h"
#include "util.h"

#define KEYBINDINGS_SCHEMA_ID "org.gnome.shell.keybindings"
#define KEYBINDING_KEY_BRIGHTNESS_UP "screen-brightness-up"
#define KEYBINDING_KEY_BRIGHTNESS_DOWN "screen-brightness-down"
#define KEYBINDING_KEY_BRIGHTNESS_UP_MONITOR "screen-brightness-up-monitor"
#define KEYBINDING_KEY_BRIGHTNESS_DOWN_MONITOR "screen-brightness-down-monitor"

/**
 * PhoshBrightnessManager:
 *
 * Manage backglight brightness
 */

#define BRIGHTNESS_STEP_AMOUNT(max) ((max) < 20 ? 1 : (max) / 20)

struct _PhoshBrightnessManager {
  GObject               parent;

  GStrv           action_names;
  GSettings      *settings;
  PhoshBacklight *backlight;
  GtkAdjustment  *adjustment;
  gulong          value_changed_id;
  gboolean        setting_brightness;
};
G_DEFINE_TYPE (PhoshBrightnessManager, phosh_brightness_manager, G_TYPE_OBJECT)


static void
on_brightness_changed (PhoshBrightnessManager *self, GParamSpec *pspec, PhoshBacklight *backlight)
{
  double value;

  g_assert (self->backlight == backlight);

  if (self->setting_brightness)
    return;

  value = 100.0 * phosh_backlight_get_relative (self->backlight);

  g_signal_handler_block (self->adjustment, self->value_changed_id);
  gtk_adjustment_set_value (self->adjustment, value);
  g_signal_handler_unblock (self->adjustment, self->value_changed_id);
}


static void
on_value_changed (PhoshBrightnessManager *self, GtkAdjustment *adjustment)
{
  double value;

  g_assert (self->adjustment == adjustment);

  if (!self->backlight)
    return;

  value = gtk_adjustment_get_value (self->adjustment) * 0.01;

  self->setting_brightness = TRUE;
  phosh_backlight_set_relative (self->backlight, value);
  self->setting_brightness = FALSE;
}


static void
set_backlight (PhoshBrightnessManager *self, PhoshBacklight *backlight)
{
  if (self->backlight == backlight)
    return;

  if (self->backlight)
    g_signal_handlers_disconnect_by_data (self->backlight, self);

  g_set_object (&self->backlight, backlight);
  if (self->backlight) {
    g_debug ("Found %s for brightness control", phosh_backlight_get_name (self->backlight));

    g_signal_connect_swapped (self->backlight,
                              "notify::brightness",
                              G_CALLBACK (on_brightness_changed),
                              self);
    on_brightness_changed (self, NULL, self->backlight);
  }
}


static void
on_primary_monitor_changed (PhoshBrightnessManager *self, GParamSpec *psepc, PhoshShell *shell)
{
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (shell);
  PhoshBacklight *backlight = NULL;

  if (monitor && monitor->backlight)
    backlight = monitor->backlight;

  /* Fall back to built in display */
  if (!backlight) {
    monitor = phosh_shell_get_builtin_monitor (shell);
    if (monitor)
      backlight = monitor->backlight;
  }

  set_backlight (self, backlight);
}


static void
adjust_brightness (PhoshBrightnessManager *self, gboolean up)
{
  PhoshShell *shell = phosh_shell_get_default ();
  int min = 0, max = 0, step, brightness;
  double percentage;

  if (!self->backlight)
    return;

  phosh_backlight_get_range (self->backlight, &min, &max);
  step = MAX (1, BRIGHTNESS_STEP_AMOUNT(max - min + 1));
  brightness = phosh_backlight_get_brightness (self->backlight);

  if (up)
    brightness += step;
  else
    brightness -= step;

  brightness = CLAMP (brightness, min, max);
  phosh_backlight_set_brightness (self->backlight, brightness);

  if (phosh_shell_get_state (shell) & PHOSH_STATE_SETTINGS)
    return;

  percentage = 100.0 * phosh_backlight_get_relative (self->backlight);
  phosh_shell_show_osd (phosh_shell_get_default (),
                        NULL,
                        "display-brightness-symbolic",
                        NULL,
                        percentage,
                        100.0);
}


static void
on_brightness_up (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (data);

  adjust_brightness (self, TRUE);
}


static void
on_brightness_down (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (data);

  adjust_brightness (self, FALSE);
}


static void
add_keybindings (PhoshBrightnessManager *self)
{
  g_autoptr (GArray) actions = g_array_new (FALSE, TRUE, sizeof (GActionEntry));
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_UP,
                               on_brightness_up);
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_DOWN,
                               on_brightness_down);
  /* TODO: use current monitor */
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_UP_MONITOR,
                               on_brightness_up);
  /* TODO: use current monitor */
  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_BRIGHTNESS_DOWN_MONITOR,
                               on_brightness_down);

  phosh_shell_add_global_keyboard_action_entries (phosh_shell_get_default (),
                                                  (GActionEntry *)actions->data,
                                                  actions->len,
                                                  self);
  self->action_names = g_strv_builder_end (builder);
}


static void
on_keybindings_changed (PhoshBrightnessManager *self)
{
  g_debug ("Updating keybindings in BrightnessManager");
  phosh_shell_remove_global_keyboard_action_entries (phosh_shell_get_default (),
                                                     self->action_names);
  g_clear_pointer (&self->action_names, g_strfreev);
  add_keybindings (self);
}


static void
phosh_brightness_manager_dispose (GObject *object)
{
  PhoshBrightnessManager *self = PHOSH_BRIGHTNESS_MANAGER (object);

  set_backlight (self, NULL);
  g_clear_pointer (&self->action_names, g_strfreev);
  g_clear_object (&self->settings);
  g_clear_signal_handler (&self->value_changed_id, self->adjustment);
  g_clear_object (&self->adjustment);

  G_OBJECT_CLASS (phosh_brightness_manager_parent_class)->dispose (object);
}


static void
phosh_brightness_manager_class_init (PhoshBrightnessManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_brightness_manager_dispose;
}


static void
phosh_brightness_manager_init (PhoshBrightnessManager *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  g_autoptr (GSettingsSchema) schema = NULL;

  self->adjustment = g_object_ref_sink (gtk_adjustment_new (0, 0, 100, 10, 10, 0));
  self->value_changed_id = g_signal_connect_swapped (self->adjustment,
                                                     "value-changed",
                                                     G_CALLBACK (on_value_changed),
                                                     self);

  /* TODO: Drop once we can rely on GNOME 49 schema */
  schema = g_settings_schema_source_lookup (source, KEYBINDINGS_SCHEMA_ID, TRUE);
  if (!schema)
    return;

  if (!g_settings_schema_has_key (schema, KEYBINDING_KEY_BRIGHTNESS_UP))
    return;

  self->settings = g_settings_new (KEYBINDINGS_SCHEMA_ID);
  g_signal_connect_object (self->settings,
                           "changed",
                           G_CALLBACK (on_keybindings_changed),
                           self,
                           G_CONNECT_SWAPPED);
  add_keybindings (self);

  g_signal_connect_object (shell,
                           "notify::primary-monitor",
                           G_CALLBACK (on_primary_monitor_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_primary_monitor_changed (self, NULL, shell);
}


PhoshBrightnessManager *
phosh_brightness_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_BRIGHTNESS_MANAGER, NULL);
}


GtkAdjustment *
phosh_brightness_manager_get_adjustment (PhoshBrightnessManager *self)
{
  g_return_val_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (self), NULL);

  return self->adjustment;
}
