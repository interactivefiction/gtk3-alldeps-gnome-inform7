#ifndef __OSXCART_RTF_LANGCODE_H__
#define __OSXCART_RTF_LANGCODE_H__

/* Copyright 2009 P. F. Chimento
This file is part of Osxcart.

Osxcart is free software: you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later 
version.

Osxcart is distributed in the hope that it will be useful, but WITHOUT ANY 
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along 
with Osxcart.  If not, see <http://www.gnu.org/licenses/>. */

#include <glib.h>

G_GNUC_INTERNAL const gchar *language_to_iso(gint wincode);
G_GNUC_INTERNAL gint language_to_wincode(const gchar *isocode);

#endif /* __OSXCART_RTF_LANGCODE_H__ */