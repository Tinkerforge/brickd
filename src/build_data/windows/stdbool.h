/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * stdbool.h: ISO C99 bool type for MSVC/WDK
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

#ifndef BRICKD_STDBOOL_H
#define BRICKD_STDBOOL_H

#ifdef _MSC_VER

typedef enum { _Bool_must_promote_to_int = -1, false = 0, true = 1 } _Bool;

#define bool _Bool
#define false 0
#define true 1
#define __bool_true_false_are_defined 1

#endif // _MSC_VER

#endif // BRICKD_STDBOOL_H
