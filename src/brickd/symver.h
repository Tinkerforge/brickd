/*
 * brickd
 * Copyright (C) 2020-2021, 2025 Matthias Bolte <matthias@tinkerforge.com>
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

// building the brickd Debian packages on Debian Trixie requires to raise
// the maximum glibc symbol version to 2.34. Debian Trixie ships glibc 2.42
// resulting in brickd linking against the versioned __libc_start_main symbol
// that got versioned in glibc 2.34 due to a backwards incompatible change in
// glibc. this means that a brickd linked against glibc >= 2.34 cannot run
// against glibc < 2.34 anymore.
//
// https://sourceware.org/git/?p=glibc.git;a=commit;h=035c012e32c11e84d64905efaf55e74f704d3668
//
// as of now brickd doesn't link against any glibc symbol newer than glibc
// 2.34. so there is currently no need to pin symbol versions using symver as
// this pinning is only done to allow building the brickd Debian packages on
// a newer Debian release while still allowing it to run the resulting binary
// on an older Debian release.
//
// building on Debian Trixie means that the resulting Debian package contains
// a brickd binary that requires glibc >= 2.34, but there is no general
// requirement for glibc >= 2.34 now. building brickd with any glibc version
// works. this is all just about the portability of the Debian packages that
// are build on Debian Trixie to older Debian releases.

#if 0
__asm__(".symver symbol,symbol@GLIBC_x.y.z"); // an example how symver is used
#endif

#endif // __GLIBC__

#endif // __linux__

#endif // BRICKD_SYMVER_H
