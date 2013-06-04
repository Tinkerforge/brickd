/*
 * driver update tool for 64-bit Windows
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * drvupd64.c: Calls UpdateDriverForPlugAndPlayDevices
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

#include <stdio.h>
#include <windows.h>
#include <newdev.h>

int main(int argc, char **argv) {
	LPCTSTR hardware_id;
	LPCTSTR full_inf_path;
	BOOL reboot_required;
	DWORD rc;

	if (argc < 3) {
		printf("error: 1\n");
		return 1;
	}

	hardware_id = argv[1];
	full_inf_path = argv[2];

	if (!UpdateDriverForPlugAndPlayDevices(NULL, hardware_id, full_inf_path,
	                                       INSTALLFLAG_FORCE, &reboot_required)) {
		rc = GetLastError();
		printf("error: %d\n", (int)rc);
		return rc;
	}

	printf("success\n");
	return 0;
}
