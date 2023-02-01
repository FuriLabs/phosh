/*
 * Copyright (C) 2022 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3-or-later
 */

#include "gm-main.h"
#include "gm-resources.h"

/**
 * gm_init:
 *
 * Call this function to initialize the library explicitly. This makes
 * the embedded device information available.
 */
void
gm_init (void)
{
  static gsize initialized = FALSE;

  if (g_once_init_enter (&initialized)) {
    /*
     * gmobile is currently meant as static library so register
     * resources explicitly.  otherwise they get dropped during static
     * linking
     */
    gm_register_resource ();
    g_once_init_leave (&initialized, TRUE);
  }
}
