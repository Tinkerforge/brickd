/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * macros.h: Preprocessor macros
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

#ifndef BRICKD_MACROS_H
#define BRICKD_MACROS_H

#ifdef __clang__
	#if __has_feature(c_static_assert)
		#define STATIC_ASSERT(condition, message) _Static_assert(condition, message)
	#else
		#define STATIC_ASSERT(condition, message) // FIXME
	#endif
	#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) // FIXME
#elif defined(__GNUC__)
	#ifndef __GNUC_PREREQ
		#define __GNUC_PREREQ(major, minor) ((((__GNUC__) << 16) + (__GNUC_MINOR__)) >= (((major) << 16) + (minor)))
	#endif
	#if __GNUC_PREREQ(4, 4)
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) __attribute__((__format__(__gnu_printf__, fmtpos, argpos)))
	#else
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) __attribute__((__format__(__printf__, fmtpos, argpos)))
	#endif
	#if __GNUC_PREREQ(4, 6)
		#define STATIC_ASSERT(condition, message) _Static_assert(condition, message)
	#else
		#define STATIC_ASSERT(condition, message) // FIXME
	#endif
#else
	#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) // FIXME
	#define STATIC_ASSERT(condition, message) // FIXME
#endif

// if __GNUC_PREREQ is not defined by now then define it to always be false
#ifndef __GNUC_PREREQ
	#define __GNUC_PREREQ(major, minor) 0
#endif

#endif // BRICKD_MACROS_H
