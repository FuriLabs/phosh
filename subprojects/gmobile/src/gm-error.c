/*
 * Copyright (C) 2022 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "gm-error.h"

/**
 * gm_error_quark:
 *
 * Gets the GM Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (gm-error-quark, gm_error)
