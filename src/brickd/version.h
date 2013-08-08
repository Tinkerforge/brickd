/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * version.h: Version information
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

#ifndef BRICKD_VERSION_H
#define BRICKD_VERSION_H

#define VERSION_MAJOR 2
#define VERSION_MINOR 0
#define VERSION_RELEASE 6

#define INT_TO_STRING_(x) #x
#define INT_TO_STRING(x) INT_TO_STRING_(x)

#define VERSION_STRING \
	INT_TO_STRING(VERSION_MAJOR) "." \
	INT_TO_STRING(VERSION_MINOR) "." \
	INT_TO_STRING(VERSION_RELEASE)

#endif // BRICKD_VERSION_H
