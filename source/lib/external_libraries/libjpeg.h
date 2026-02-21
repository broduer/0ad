/* Copyright (c) 2022 Wildfire Games
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: © 2022 Wildfire Games
 */

#ifndef INCLUDED_LIBJPEG
#define INCLUDED_LIBJPEG

extern "C" {
// we are not a core library module, so don't define JPEG_INTERNALS
#include <jpeglib.h>
#include <jerror.h>
}

#endif	// #ifndef INCLUDED_LIBJPEG
