/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "backlight.h"

#include <gio/gio.h>

G_BEGIN_DECLS

struct _PhoshBacklightClass {
  GObjectClass parent_class;

  void (* set_brightness) (PhoshBacklight       *backlight,
                           int                  brightness_target,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data);

  int (* set_brightness_finish) (PhoshBacklight  *backlight,
                                 GAsyncResult    *result,
                                 GError         **error);
};

void phosh_backlight_backend_update_brightness (PhoshBacklight *self, int brightness);
void phosh_backlight_set_range (PhoshBacklight *self, int min, int max);
void phosh_backlight_set_name (PhoshBacklight *self, const char *name);

G_END_DECLS
