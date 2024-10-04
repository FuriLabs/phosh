/*
 * Copyright (C) 2024 Bardia Moshiri
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <fakeshell@bardia.tech>
 */

#include <gtk/gtk.h>

#include "quick-setting.h"

#pragma once

G_BEGIN_DECLS

#define PHOSH_TYPE_AIRPLANE_MODE_QUICK_SETTING (phosh_airplane_mode_quick_setting_get_type ())
G_DECLARE_FINAL_TYPE (PhoshAirplaneModeQuickSetting,
                      phosh_airplane_mode_quick_setting,
                      PHOSH, AIRPLANE_MODE_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
