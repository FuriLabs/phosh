/*
 * Copyright (C) 2022 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "gm-display-panel.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define GM_TYPE_DEVICE_INFO (gm_device_info_get_type ())

G_DECLARE_FINAL_TYPE (GmDeviceInfo, gm_device_info, GM, DEVICE_INFO, GObject)

GmDeviceInfo    *gm_device_info_new (const char * const *compatibles);
GmDisplayPanel  *gm_device_info_get_display_panel (GmDeviceInfo *self);

G_END_DECLS
