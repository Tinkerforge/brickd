/*
 * brickd
 * Copyright (C) 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * app_service.h: AppService based I/O device for Universal Windows Platform
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

#ifndef BRICKD_APP_SERVICE_H
#define BRICKD_APP_SERVICE_H

#include <agile.h>

#define APP_SERVICE_MAX_CALLER_LENGTH 128

typedef Platform::Agile<Windows::ApplicationModel::AppService::AppServiceConnection ^> AgileAppServiceConnection;
typedef Platform::Agile<Windows::ApplicationModel::Background::BackgroundTaskDeferral ^> AgileBackgroundTaskDeferral;

#ifdef __cplusplus
extern "C" {
#endif

#include <daemonlib/io.h>
#include <daemonlib/pipe.h>

typedef struct {
	IO base;

	char caller[APP_SERVICE_MAX_CALLER_LENGTH]; // for display purpose
	Pipe read_pipe;
	Pipe write_pipe;
	Pipe close_pipe;
	AgileBackgroundTaskDeferral deferral;
	AgileAppServiceConnection connection;
} AppService_;

int app_service_create(AppService_ *app_service, const char *caller,
                       AgileBackgroundTaskDeferral deferral,
                       AgileAppServiceConnection connection);
void app_service_destroy(AppService_ *app_service);

int app_service_read(AppService_ *app_service, void *buffer, int length);
int app_service_write(AppService_ *app_service, const void *buffer, int length);

#ifdef __cplusplus
}
#endif

#endif // BRICKD_APP_SERVICE_H
