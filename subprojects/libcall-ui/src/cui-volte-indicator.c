/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2024 Furi Labs
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#include "cui-config.h"

#include "cui-volte-indicator-priv.h"

#include <glib/gi18n-lib.h>

struct _CuiVolteIndicator {
  GtkStack   parent_instance;

  GtkStack  *stack;
  GtkBox    *is_volte_enabled;
  GtkBox    *is_not_volte_enabled;
};

G_DEFINE_TYPE (CuiVolteIndicator, cui_volte_indicator, GTK_TYPE_STACK);

enum {
  PROP_0,
  PROP_VOLTE_ENABLED,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


void
cui_volte_indicator_set_volte_enabled (CuiVolteIndicator *self,
                                       gboolean           enabled)
{
  g_return_if_fail (CUI_IS_VOLTE_INDICATOR (self));

  enabled = !!enabled;

  gtk_stack_set_visible_child (
    self->stack,
    GTK_WIDGET (enabled ? self->is_volte_enabled : self->is_not_volte_enabled));
}


gboolean
cui_volte_indicator_get_volte_enabled (CuiVolteIndicator *self)
{
  g_return_val_if_fail (CUI_IS_VOLTE_INDICATOR (self), FALSE);

  return gtk_stack_get_visible_child (GTK_STACK (self->stack)) == GTK_WIDGET (self->is_volte_enabled);
}


static void
cui_volte_indicator_init (CuiVolteIndicator *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  CuiVolteIndicator *self = CUI_VOLTE_INDICATOR (object);

  switch (property_id) {
  case PROP_VOLTE_ENABLED:
    cui_volte_indicator_set_volte_enabled (self, g_value_get_boolean (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
get_property (GObject    *object,
              guint       property_id,
              GValue     *value,
              GParamSpec *pspec)
{
  CuiVolteIndicator *self = CUI_VOLTE_INDICATOR (object);

  switch (property_id) {
  case PROP_VOLTE_ENABLED:
    g_value_set_boolean (value, cui_volte_indicator_get_volte_enabled (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
cui_volte_indicator_dispose (GObject *object)
{
  CuiVolteIndicator *self = CUI_VOLTE_INDICATOR (object);

  GtkWidget *stack= GTK_WIDGET (self->stack);

  g_clear_pointer (&stack, gtk_widget_unparent);

  G_OBJECT_CLASS (cui_volte_indicator_parent_class)->dispose (object);
}


static void
cui_volte_indicator_class_init (CuiVolteIndicatorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = set_property;
  object_class->get_property = get_property;

  object_class->dispose = cui_volte_indicator_dispose;

  props[PROP_VOLTE_ENABLED] =
    g_param_spec_boolean ("volte-enabled",
                          "VoLTE-enabled",
                          "If this call is VoLTE-enabled",
                          FALSE,
                          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/CallUI/ui/cui-volte-indicator.ui");
  gtk_widget_class_bind_template_child (widget_class, CuiVolteIndicator, is_volte_enabled);
  gtk_widget_class_bind_template_child (widget_class, CuiVolteIndicator, is_not_volte_enabled);
  gtk_widget_class_bind_template_child (widget_class, CuiVolteIndicator, stack);
}
