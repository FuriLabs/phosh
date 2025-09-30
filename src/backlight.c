/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-backlight"

#include "phosh-config.h"

#include "backlight-priv.h"

#include <gtk/gtk.h>

/**
 * PhoshBacklight:
 *
 * A `PhoshMonitor`'s backlight. Derived classes implement the actual
 * backlight handling.
 */

enum {
  PROP_0,
  PROP_BRIGHTNESS,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PhoshBacklightPrivate {
  GObject       parent;

  char         *name;

  gboolean      pending;
  int           min_brightness;
  int           max_brightness;
  int           target_brightness;

  GCancellable *cancel;
} PhoshBacklightPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (PhoshBacklight, phosh_backlight, G_TYPE_OBJECT)


static void
phosh_backlight_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);

  switch (property_id) {
  case PROP_BRIGHTNESS:
    phosh_backlight_set_brightness (self, g_value_get_int (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_backlight_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  switch (property_id) {
  case PROP_BRIGHTNESS:
    g_value_set_int (value, priv->target_brightness);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_backlight_dispose (GObject *object)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_cancellable_cancel (priv->cancel);
  g_clear_object (&priv->cancel);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (phosh_backlight_parent_class)->dispose (object);
}


static void
phosh_backlight_class_init (PhoshBacklightClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_backlight_get_property;
  object_class->set_property = phosh_backlight_set_property;
  object_class->dispose = phosh_backlight_dispose;

  props[PROP_BRIGHTNESS] =
    g_param_spec_int ("brightness", "", "",
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_backlight_init (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  priv->cancel = g_cancellable_new ();
}

/**
 * phosh_backlight_backend_update_brightness:
 * @self: The backlight
 * @brightness: the brightness
 *
 * This is invoked by the concrete backend implementation when the
 * hardware changed brightness.
 */
void
phosh_backlight_backend_update_brightness (PhoshBacklight *self, int brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  int new_brightness;

  if (priv->target_brightness == brightness)
    return;

  new_brightness = CLAMP (brightness, priv->min_brightness, priv->max_brightness);

  if (brightness != new_brightness)
    g_warning ("Trying to set out-of-range brightness %d on %s", brightness, priv->name);

  priv->target_brightness = new_brightness;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BRIGHTNESS]);
}


void
phosh_backlight_get_range (PhoshBacklight *self, int *min_brightness, int *max_brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_BACKLIGHT (self));

  if (min_brightness)
    *min_brightness = priv->min_brightness;

  if (max_brightness)
    *max_brightness = priv->max_brightness;
}


void
phosh_backlight_set_range (PhoshBacklight *self, int min, int max)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  priv->min_brightness = min;
  priv->max_brightness = max;
}


int
phosh_backlight_get_brightness (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), -1);

  return priv->target_brightness;
}


static void
on_brightness_set (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (source_object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  g_autoptr (GError) err = NULL;
  int brightness;

  priv->pending = FALSE;

  brightness = PHOSH_BACKLIGHT_GET_CLASS (self)->set_brightness_finish (self, res, &err);
  if (brightness < 0) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    g_warning ("Setting backlight on %s failed: %s", priv->name, err->message);
    return;
  }

  /* Brightness got updated from the system and we tried to set it at
   * the same time. Let's try to set the brightness the system was
   * setting to make sure we're in the correct state. */
  if (priv->target_brightness != brightness) {
    priv->pending = TRUE;

    PHOSH_BACKLIGHT_GET_CLASS (self)->set_brightness (self,
                                                      priv->target_brightness,
                                                      priv->cancel,
                                                      on_brightness_set,
                                                      NULL);
  }
}


void
phosh_backlight_set_brightness (PhoshBacklight *self, int brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  int new_brightness;

  g_return_if_fail (PHOSH_IS_BACKLIGHT (self));

  new_brightness = CLAMP (brightness, priv->min_brightness, priv->max_brightness);

  if (brightness != new_brightness)
    g_warning ("Trying to set out-of-range brightness %d on %s", brightness, priv->name);

  if (priv->target_brightness == new_brightness)
    return;

  g_debug ("Setting target brightness to %d", brightness);
  priv->target_brightness = new_brightness;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BRIGHTNESS]);

  if (!priv->pending) {
    priv->pending = TRUE;

    PHOSH_BACKLIGHT_GET_CLASS (self)->set_brightness (self,
                                                      priv->target_brightness,
                                                      priv->cancel,
                                                      on_brightness_set,
                                                      NULL);
  }
}


const char *
phosh_backlight_get_name (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), NULL);

  return priv->name;
}


void
phosh_backlight_set_name (PhoshBacklight *self, const char *name)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_set_str (&priv->name, name);
}
