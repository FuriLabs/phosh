/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2024 Furi Labs
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CUI_TYPE_VOLTE_INDICATOR (cui_volte_indicator_get_type ())

G_DECLARE_FINAL_TYPE (CuiVolteIndicator, cui_volte_indicator, CUI, VOLTE_INDICATOR, GtkStack);

void     cui_volte_indicator_set_volte_enabled (CuiVolteIndicator *self,
                                                gboolean           enabled);
gboolean cui_volte_indicator_get_volte_enabled (CuiVolteIndicator *self);

G_END_DECLS
