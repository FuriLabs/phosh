/*
 * Copyright (C) 2024 Bardia Moshiri
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <fakeshell@bardia.tech>
 */

#include "airplane-mode-quick-setting.h"

#include "plugin-shell.h"
#include "quick-setting.h"
#include "status-icon.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#define RFKILL_NAME "org.gnome.SettingsDaemon.Rfkill"
#define RFKILL_PATH "/org/gnome/SettingsDaemon/Rfkill"
#define RFKILL_IFACE "org.gnome.SettingsDaemon.Rfkill"

/**
 * PhoshAirplaneModeQuickSetting:
 *
 * Enable or disable airplane_mode mode
 */
struct _PhoshAirplaneModeQuickSetting {
  PhoshQuickSetting        parent;

  PhoshStatusIcon         *info;
  GDBusProxy              *proxy;

  gboolean                enabled;
};

G_DEFINE_TYPE (PhoshAirplaneModeQuickSetting, phosh_airplane_mode_quick_setting, PHOSH_TYPE_QUICK_SETTING);

static gboolean
get_rfkill_property (const gchar *property)
{
  GDBusConnection *connection;
  GVariant *result;
  gboolean value = FALSE;
  GError *error = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL) {
    g_warning ("Failed to connect to session bus: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_connection_call_sync (connection,
                                        "org.gnome.SettingsDaemon.Rfkill",
                                        "/org/gnome/SettingsDaemon/Rfkill",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        g_variant_new ("(ss)",
                                                       "org.gnome.SettingsDaemon.Rfkill",
                                                       property),
                                        G_VARIANT_TYPE("(v)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        3000,
                                        NULL,
                                        &error);

  if (error) {
    g_warning ("Failed to get property %s: %s", property, error->message);
    g_clear_error (&error);
  } else if (result != NULL) {
    GVariant *v;
    g_variant_get (result, "(v)", &v);
    value = g_variant_get_boolean (v);
    g_variant_unref (v);
    g_variant_unref (result);
  }

  g_object_unref (connection);

  return value;
}

static void
update_ui (PhoshAirplaneModeQuickSetting *self)
{
  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), self->enabled);
  if (self->enabled) {
    phosh_status_icon_set_icon_name (self->info, "airplane-mode-symbolic");
    phosh_status_icon_set_info (self->info, _("Airplane Mode On"));
  } else {
    phosh_status_icon_set_icon_name (self->info, "network-cellular-connected-symbolic");
    phosh_status_icon_set_info (self->info, _("Airplane Mode Off"));
  }
}

static void
on_clicked (PhoshAirplaneModeQuickSetting *self)
{
  self->enabled = !phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));

  g_debug ("Airplane Mode enabled: %s", self->enabled ? "true" : "false");

  update_ui (self);

  g_dbus_proxy_call (self->proxy,
                     "org.freedesktop.DBus.Properties.Set",
                     g_variant_new_parsed ("('org.gnome.SettingsDaemon.Rfkill',"
                                           "'AirplaneMode', %v)",
                                           g_variant_new_boolean (self->enabled)),
                     G_DBUS_CALL_FLAGS_NONE,
                     3000,
                     NULL,
                     NULL,
                     NULL);
}

static void
phosh_airplane_mode_quick_setting_class_init (PhoshAirplaneModeQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/airplane-mode-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshAirplaneModeQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}

static void
phosh_airplane_mode_quick_setting_init (PhoshAirplaneModeQuickSetting *self)
{
  gboolean enabled, hw_enabled;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               NULL,
                                               RFKILL_NAME,
                                               RFKILL_PATH,
                                               RFKILL_IFACE,
                                               NULL,
                                               NULL);

  enabled = get_rfkill_property ("AirplaneMode");
  hw_enabled = get_rfkill_property ("HardwareAirplaneMode");

  self->enabled = enabled | hw_enabled;

  update_ui (self);
}
