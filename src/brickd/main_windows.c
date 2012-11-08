/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_windows.c: Brick Daemon starting point for Windows
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
#include <dbt.h>

#include "event.h"
#include "log.h"
#include "usb.h"
#include "utils.h"
#include "version.h"


//#include <usbiodef.h>
/* A5DCBF10-6530-11D2-901F-00C04FB951ED */
/*#undef EXTERN_C
#define EXTERN_C
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, \
			 0xC0, 0x4F, 0xB9, 0x51, 0xED);*/

static const GUID GUID_DEVINTERFACE_USB_DEVICE = { 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00,
			 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

static char *service_name = "Brick Daemon";
static SERVICE_STATUS service_status;
static SERVICE_STATUS_HANDLE service_status_handle = 0;

DWORD WINAPI service_control_handler( DWORD dwControl,
 DWORD dwEventType,
 LPVOID lpEventData,
  LPVOID lpContext )
{
	log_info("ServiceControlHandler: %u\n", GetCurrentThreadId());

	switch (dwControl) {
	case SERVICE_CONTROL_INTERROGATE:
		return NO_ERROR;

	case SERVICE_CONTROL_SHUTDOWN:
	case SERVICE_CONTROL_STOP:
		service_status.dwCurrentState = SERVICE_STOP_PENDING;

		SetServiceStatus(service_status_handle, &service_status);

		event_stop();

		return NO_ERROR;

	case SERVICE_CONTROL_DEVICEEVENT:
		//	PDEV_BROADCAST_HDR pBroadcastHdr = (PDEV_BROADCAST_HDR)lpEventData;

		switch (dwEventType) {
		case DBT_DEVICEARRIVAL:
			/*::MessageBox(NULL, "A Device has been plugged in.", "Pounce", MB_OK | MB_ICONINFORMATION);

			switch (pBroadcastHdr->dbch_devicetype)
			{
			case DBT_DEVTYP_DEVICEINTERFACE:
				PDEV_BROADCAST_DEVICEINTERFACE pDevInt = (PDEV_BROADCAST_DEVICEINTERFACE)pBroadcastHdr;

				if (::IsEqualGUID(pDevInt->dbcc_classguid, GUID_DEVINTERFACE_VOLUME))
				{
					PDEV_BROADCAST_VOLUME pVol = (PDEV_BROADCAST_VOLUME)pDevInt;

					char szMsg[80];
					char cDriveLetter = ::GetDriveLetter(pVol->dbcv_unitmask);

					::wsprintfA(szMsg, "USB disk drive with the drive letter '%c:' has been inserted.", cDriveLetter);
					::MessageBoxA(NULL, szMsg, "Pounce", MB_OK | MB_ICONINFORMATION);
				}
			}*/

			log_info("DBT_DEVICEARRIVAL %u\n", GetCurrentThreadId());

			break;

		case DBT_DEVICEREMOVECOMPLETE:
			log_info("DBT_DEVICEREMOVECOMPLETE %u\n", GetCurrentThreadId());


			break;
		}

		return NO_ERROR;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv) {
	int rc;
	DEV_BROADCAST_DEVICEINTERFACE notification_filter;

	log_info("ServiceMain: %u\n", GetCurrentThreadId());

	// initialise service status
	service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	service_status.dwCurrentState = SERVICE_STOPPED;
	service_status.dwControlsAccepted = 0;
	service_status.dwWin32ExitCode = NO_ERROR;
	service_status.dwServiceSpecificExitCode = NO_ERROR;
	service_status.dwCheckPoint = 0;
	service_status.dwWaitHint = 0;

	service_status_handle = RegisterServiceCtrlHandlerEx(service_name, service_control_handler, NULL);

	if (service_status_handle == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register service control handler: %d (%d)",
		          get_errno_name(rc), rc);

		return;
	}


	// service is starting
	service_status.dwCurrentState = SERVICE_START_PENDING;

	SetServiceStatus(service_status_handle, &service_status);

	// do initialisation here

	// INITIAL USB DEVICE enumeration



	ZeroMemory(&notification_filter, sizeof(notification_filter));

	notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	notification_filter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

	/* handle */RegisterDeviceNotification(
		service_status_handle,					   // events recipient
		&notification_filter,		// type of device
		DEVICE_NOTIFY_SERVICE_HANDLE // type of recipient handle
		);

	// running
	service_status.dwControlsAccepted |= (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
	service_status.dwCurrentState = SERVICE_RUNNING;

	SetServiceStatus( service_status_handle, &service_status );

	event_run(); // FIXME check return value and set exit code


	// service was stopped, FIXME: move after the event_stop() call
	service_status.dwCurrentState = SERVICE_STOP_PENDING;

	SetServiceStatus(service_status_handle, &service_status);

	// do cleanup here

	//UnregisterDeviceNotification (handle)

	// service is now stopped
	service_status.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
	service_status.dwCurrentState = SERVICE_STOPPED;

	SetServiceStatus(service_status_handle, &service_status);
}

void service_run(void) {
	SERVICE_TABLE_ENTRY service_table[2];
	int rc;

	service_table[0].lpServiceName = service_name;
	service_table[0].lpServiceProc = service_main;

	service_table[1].lpServiceName = NULL;
	service_table[1].lpServiceProc = NULL;

	if (!StartServiceCtrlDispatcher(service_table)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not start service control dispatcher: %s (%d)",
		          get_errno_name(rc), rc);
	}
}

void service_install(void) {
	SC_HANDLE service_control_manager = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);

		printf("0\n");
	if ( service_control_manager )
	{
		char path[ 512 + 1 ];
		printf("1\n");
		if ( GetModuleFileName( 0, path, sizeof(path)/sizeof(path[0]) ) > 0 )
		{
			SC_HANDLE service = CreateService( service_control_manager,
							service_name, service_name,
							SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
							SERVICE_AUTO_START, SERVICE_ERROR_IGNORE, path,
							0, 0, 0, 0, 0 );
		printf("2\n");
			if ( service ) {
				SERVICE_DESCRIPTION description;
				description.lpDescription = "Brick Daemon is a bridge between USB devices ('Bricks') and TCP/IP sockets. It can be used to read out and control Bricks.";

				/*BOOL*/ ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION,&description);



		printf("3\n");
			  /*BOOL*/ StartService(service, 0, NULL);


				CloseServiceHandle( service );
			}
		}

		CloseServiceHandle( service_control_manager );
	}
}

void service_uninstall(void) {
	SC_HANDLE service_control_manager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);

		printf("a\n");
	if ( service_control_manager )
	{
		SC_HANDLE service = OpenService( service_control_manager,
			service_name, SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE );
		printf("b\n");
		if ( service )
		{
			SERVICE_STATUS service_status;
		printf("c\n");
				/*BOOL */ ControlService(service, SERVICE_CONTROL_STOP, &service_status);
			 while (service_status.dwCurrentState != SERVICE_STOPPED) {
			printf("d\n");
				if ( QueryServiceStatus( service, &service_status ) )
				{



					if ( service_status.dwCurrentState == SERVICE_STOPPED ) {
						DeleteService( service );

			printf("e\n");
					}
				}
			}

			CloseServiceHandle( service );
		}

		CloseServiceHandle( service_control_manager );
	}
}

int main(int argc, char **argv) {
	if ( argc > 1 && strcmp( argv[1], "install" ) == 0 )
	{
		service_install();
	}
	else if ( argc > 1 && strcmp( argv[1], "uninstall" ) == 0 )
	{
		service_uninstall();
	}
	else
	{
		FILE *fp = fopen("C:\\tf\\brickd.log", "a+");

		log_init();
		log_set_level(LOG_LEVEL_DEBUG);
		log_set_stream(fp);


		log_info("brickd %s started", VERSION_STRING);


		log_info("main: %u\n", GetCurrentThreadId());
		service_run();
	}

	return 0;
}














/*




static void print_usage(void) {
	printf("Usage: brickd.exe --version\n");
}

int __main(int argc, char **argv) {
	log_init();
	log_set_level(LOG_LEVEL_DEBUG);

	if (argc < 2) {
		print_usage();
		return 0;
	}

	log_info("Brick Daemon %s started", VERSION_STRING);

	if (usb_init() < 0) {
		// FIXME: handle error
	}





	usb_exit();

	log_info("Brick Daemon %s stopped", VERSION_STRING);

	return 0;
}*/
