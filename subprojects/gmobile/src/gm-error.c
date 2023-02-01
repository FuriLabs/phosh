/*
 * Copyright (C) 2022 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "gm-error.h"

GQuark
gm_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string("gm");

  return quark;
}
