/*
 * brickd
 * Copyright (C) 2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * symver.h: force linking to older glibc symbols to lower glibc dependency
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BRICKD_SYMVER_H
#define BRICKD_SYMVER_H

#ifdef __linux__

#include <features.h>

#ifdef __GLIBC__

#if defined __aarch64__
// do nothing, because arm64 requires glibc >= 2.17 anyway
#elif defined __arm__
__asm__(".symver memcpy,memcpy@GLIBC_2.4");
#else
__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
#endif

#endif // __GLIBC__

#endif // __linux__

#endif // BRICKD_SYMVER_H
