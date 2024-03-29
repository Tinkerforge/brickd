/*
 * brickd
 * Copyright (C) 2012-2014, 2019, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * service.h: Windows service specific functions
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

#ifndef BRICKD_SERVICE_H
#define BRICKD_SERVICE_H

#include <stdbool.h>

int service_init(LPHANDLER_FUNCTION_EX handler);
void service_exit(void);
int service_is_running(void);
void service_set_status(DWORD status, DWORD exit_code);
SERVICE_STATUS_HANDLE service_get_status_handle(void);
char *service_get_name(void);
int service_install(const char *debug_filter);
int service_uninstall(void);

#endif // BRICKD_SERVICE_H
