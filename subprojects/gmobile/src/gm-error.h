/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include <glib.h>

typedef enum {
  GM_ERROR_PARSING_FAILED = 1,
} GmError;

#define GM_ERROR (gm_error_quark())

GQuark gm_error_quark (void);
