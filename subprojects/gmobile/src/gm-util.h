/*
 * Copyright (C) 2023 Guido Günther
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#if !defined(_GMOBILE_INSIDE) && !defined(GMOBILE_COMPILATION)
#error "Only <gmobile.h> can be included directly."
#endif

#define gm_str_is_null_or_empty(x) \
  ((x) == NULL || (x)[0] == '\0')
#define gm_strv_is_null_or_empty(x) \
  ((x) == NULL || (x)[0] == NULL)
