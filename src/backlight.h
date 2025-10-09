/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_BACKLIGHT (phosh_backlight_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhoshBacklight, phosh_backlight, PHOSH, BACKLIGHT, GObject)

int          phosh_backlight_get_brightness (PhoshBacklight *self);
void         phosh_backlight_set_brightness (PhoshBacklight *self, int brightness);
double       phosh_backlight_get_relative (PhoshBacklight *self);
void         phosh_backlight_set_relative (PhoshBacklight *self, double val);
void         phosh_backlight_get_range (PhoshBacklight *self,
                                        int            *min_brightness,
                                        int            *max_brightness);
const char * phosh_backlight_get_name (PhoshBacklight *self);

G_END_DECLS
