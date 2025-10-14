/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_BRIGHTNESS_MANAGER (phosh_brightness_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshBrightnessManager, phosh_brightness_manager, PHOSH, BRIGHTNESS_MANAGER, GObject)

PhoshBrightnessManager *phosh_brightness_manager_new (void);

G_END_DECLS
