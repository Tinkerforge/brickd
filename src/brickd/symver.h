/*
 * brickd
 * Copyright (C) 2020-2021 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2021 Andreas Schwab <schwab@suse.de>
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

#if defined(__linux__) && !defined(DAEMONLIB_WITH_STATIC)

#include <features.h>

#ifdef __GLIBC__

#if defined __aarch64__
// do nothing, because arm64 requires glibc >= 2.17 anyway
#elif defined __riscv
// do nothing, because riscv requires glibc >= 2.27 anyway
#elif defined __arm__
__asm__(".symver fcntl,fcntl@GLIBC_2.4");
__asm__(".symver memcpy,memcpy@GLIBC_2.4");
#elif defined __i386__
__asm__(".symver clock_gettime,clock_gettime@GLIBC_2.2");
__asm__(".symver clock_nanosleep,clock_nanosleep@GLIBC_2.2");
__asm__(".symver fcntl,fcntl@GLIBC_2.0");
#else
__asm__(".symver clock_gettime,clock_gettime@GLIBC_2.2.5");
__asm__(".symver clock_nanosleep,clock_nanosleep@GLIBC_2.2.5");
__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
#endif

#endif // __GLIBC__

#endif // __linux__

#endif // BRICKD_SYMVER_H
