/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-settings-brightness"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "backlight.h"
#include "monitor/monitor.h"
#include "shell-priv.h"
#include "settings/brightness.h"


static PhoshBacklight *backlight;
static gboolean setting_brightness;
static gulong scale_handler_id;


static void
on_brightness_changed (PhoshBacklight *backlight_,
                       GParamSpec     *pspec,
                       gpointer        user_data)
{
  GtkScale *scale = GTK_SCALE (user_data);
  double value;

  g_assert (backlight == backlight_);

  if (setting_brightness)
    return;

  value = 100.0 * phosh_backlight_get_relative (backlight);

  g_signal_handler_block (G_OBJECT (scale), scale_handler_id);
  gtk_range_set_value (GTK_RANGE (scale), value);
  g_signal_handler_unblock (G_OBJECT (scale), scale_handler_id);
}


static PhoshBacklight *
find_backlight (void)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (shell);

  if (monitor->backlight)
    return g_object_ref (PHOSH_BACKLIGHT (monitor->backlight));

  monitor = phosh_shell_get_builtin_monitor (shell);
  if (monitor && monitor->backlight)
    return g_object_ref (PHOSH_BACKLIGHT (monitor->backlight));

  return NULL;
}


void
brightness_init (GtkScale *scale, gulong handler_id)
{
  int brightness;

  scale_handler_id = handler_id;

  backlight = find_backlight ();
  if (!backlight) {
    /* TODO: should hide UI element in that case */
    return;
  }

  g_debug ("Found backlight %s", phosh_backlight_get_name (backlight));
  brightness = phosh_backlight_get_brightness (backlight);
  setting_brightness = TRUE;
  gtk_range_set_value (GTK_RANGE (scale), brightness);
  setting_brightness = FALSE;

  g_signal_connect (backlight,
                    "notify::brightness",
                    G_CALLBACK (on_brightness_changed),
                    scale);
  on_brightness_changed (backlight, NULL, scale);

  /* TODO: Need to track monitor changes */
}


void
brightness_set (int value)
{
  if (!backlight)
    return;

  if (setting_brightness)
    return;

  setting_brightness = TRUE;

  phosh_backlight_set_relative (backlight, value * 0.01);
  setting_brightness = FALSE;
}


void
brightness_dispose (void)
{
  g_clear_object (&backlight);
}
