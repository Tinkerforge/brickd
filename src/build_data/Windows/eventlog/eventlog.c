/*
 * event log viewer for brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * eventlog.c: Shows event log for brickd
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

static const char *_title = "Brick Daemon - Event Log Viewer 1.0";
static HINSTANCE _hinstance = NULL;
static HANDLE _event_log = NULL;
static HWND _hwnd = NULL;
static HWND _list_view = NULL;
static PBYTE _record_buffer = NULL;

#define MAX_TIMESTAMP_LEN (19 + 1) // yyyy-mm-dd hh:mm:ss
#define MAX_RECORD_BUFFER_SIZE  0x10000 // 64K

static void report_error(const char *format, ...) {
	char message[1024 + 1];
	va_list arguments;

	va_start(arguments, format);
	_vsnprintf_s(message, sizeof(message), sizeof(message) - 1, format, arguments);
	va_end(arguments);

	MessageBox(NULL, message, _title, MB_ICONERROR);
}

static const char *get_error_name(int error_code) {
	#define ERROR_NAME(code) case code: return #code

	switch (error_code) {
	ERROR_NAME(ERROR_INVALID_DATA);
	ERROR_NAME(ERROR_ACCESS_DENIED);
	ERROR_NAME(ERROR_INVALID_HANDLE);
	ERROR_NAME(ERROR_INVALID_NAME);
	ERROR_NAME(ERROR_INVALID_PARAMETER);
	ERROR_NAME(ERROR_INSUFFICIENT_BUFFER);
	ERROR_NAME(ERROR_INVALID_WINDOW_HANDLE);

	// FIXME

	default: return "<unknown>";
	}
}

enum {
	ID_FILE_SAVE,
	ID_FILE_EXIT
};

void create_menu(void) {
	HMENU hmenu = CreateMenu();
	HMENU hsubmenu = CreatePopupMenu();

	AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT)hsubmenu, "&File");
	AppendMenu(hsubmenu, MF_STRING, ID_FILE_SAVE, "&Save...");
	AppendMenu(hsubmenu, MF_STRING, ID_FILE_EXIT, "&Exit");

	SetMenu(_hwnd, hmenu);
}

int create_list_view(void) {
	INITCOMMONCONTROLSEX icex;
	RECT client;
	LVCOLUMN lvc;

	icex.dwICC = ICC_LISTVIEW_CLASSES;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);

	if (!InitCommonControlsEx(&icex)) {
		report_error("Could not initialize common controls");
		return -1;
	}

	GetClientRect (_hwnd, &client);

	_list_view = CreateWindow(WC_LISTVIEW,
	                          "",
	                          WS_VISIBLE | WS_CHILD | LVS_REPORT |
	                          LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
	                          0, 0,
	                          client.right - client.left,
	                          client.bottom - client.top,
	                          _hwnd,
	                          NULL,
	                          _hinstance,
	                          NULL);

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.fmt = LVCFMT_LEFT;

	lvc.iSubItem = 0;
	lvc.cx = 120;
	lvc.pszText = "Timestamp";

	if (ListView_InsertColumn(_list_view, 0, &lvc) < 0) {
		report_error("Could not insert list view column");
		return -1;
	}

	lvc.iSubItem = 1;
	lvc.cx = 60;
	lvc.pszText = "Type";

	if (ListView_InsertColumn(_list_view, 1, &lvc) < 0) {
		report_error("Could not insert list view column");
		return -1;
	}

	lvc.iSubItem = 2;
	lvc.cx = 575;
	lvc.pszText = "Message";

	if (ListView_InsertColumn(_list_view, 2, &lvc) < 0) {
		report_error("Could not insert list view column");
		return -1;
	}

	SetFocus(_list_view);

	return 0;
}

void append_item(const char *timestamp, const char *type, const char *message) {
	LVITEM lvi;

	lvi.mask = LVIF_TEXT;

	lvi.iItem = 0;
	lvi.iSubItem = 0;
	lvi.pszText = (char *)timestamp;
	ListView_InsertItem(_list_view, &lvi);

	lvi.iSubItem = 1;
	lvi.pszText = (char *)type;
	ListView_SetItem(_list_view, &lvi);

	lvi.iSubItem = 2;
	lvi.pszText = (char *)message;
	ListView_SetItem(_list_view, &lvi);
}

static void get_timestamp(const DWORD time, char *buffer)
{
	ULONGLONG timestamp = 0;
	ULONGLONG secs_to_1970 = 116444736000000000;
	SYSTEMTIME st;
	FILETIME ft, ft_local;

	timestamp = Int32x32To64(time, 10000000) + secs_to_1970;
	ft.dwHighDateTime = (DWORD)((timestamp >> 32) & 0xFFFFFFFF);
	ft.dwLowDateTime = (DWORD)(timestamp & 0xFFFFFFFF);

	FileTimeToLocalFileTime(&ft, &ft_local);
	FileTimeToSystemTime(&ft_local, &st);

	_snprintf(buffer, MAX_TIMESTAMP_LEN, "%d-%02d-%02d %.2d:%.2d:%.2d",
	          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static void read_events(void) {
	DWORD status = ERROR_SUCCESS;
	DWORD bytes_to_read = 0;
	DWORD bytes_read = 0;
	DWORD minimum_bytes_to_read = 0;
	PBYTE temp = NULL;

	if (_record_buffer == NULL) {
		bytes_to_read = MAX_RECORD_BUFFER_SIZE;
		_record_buffer = (PBYTE)malloc(bytes_to_read);

		if (_record_buffer == NULL) {
			report_error("Could not to allocate record buffer");
			return;
		}
	}

	while (status == ERROR_SUCCESS) {
		if (!ReadEventLog(_event_log,
		                  EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ,
		                  0,
		                  _record_buffer,
		                  bytes_to_read,
		                  &bytes_read,
		                  &minimum_bytes_to_read)) {
			status = GetLastError();

			if (status == ERROR_INSUFFICIENT_BUFFER) {
				status = ERROR_SUCCESS;
				temp = (PBYTE)realloc(_record_buffer, minimum_bytes_to_read);

				if (temp == NULL) {
					report_error("Could not to reallocate record buffer to %u bytes",
					             minimum_bytes_to_read);

					return;
				}

				_record_buffer = temp;
				bytes_to_read = minimum_bytes_to_read;
			} else if (status != ERROR_HANDLE_EOF) {
				report_error("Could not read event log: %s (%d)",
				             get_error_name(status), status);

				return;
			}
		} else {
			PBYTE record = _record_buffer;
			PBYTE end_of_records = _record_buffer + bytes_read;
			char timestamp[MAX_TIMESTAMP_LEN];
			const char *type;
			const char *message;

			while (record < end_of_records) {
				if (strcmp((const char *)(record + sizeof(EVENTLOGRECORD)), "Brick Daemon") == 0) {
					get_timestamp(((PEVENTLOGRECORD)record)->TimeGenerated, timestamp);

					switch (((PEVENTLOGRECORD)record)->EventType) {
					case EVENTLOG_ERROR_TYPE:
						type = "Error";
						break;

					case EVENTLOG_WARNING_TYPE:
						type = "Warning";
						break;

					case EVENTLOG_INFORMATION_TYPE:
						type = "Information";
						break;

					case EVENTLOG_AUDIT_SUCCESS:
						type = "Audit Success";
						break;

					case EVENTLOG_AUDIT_FAILURE:
						type = "Audit Failure";
						break;

					default:
						type = "<unknown>";
						break;
					}

					if (((PEVENTLOGRECORD)record)->NumStrings > 0) {
						message = (const char *)(record + ((PEVENTLOGRECORD)record)->StringOffset);
					} else {
						message = "<unknown>";
					}

					append_item(timestamp, type, message);
				}

				record += ((PEVENTLOGRECORD)record)->Length;
			}
		}
	}
}

static void save_events(void) {
	char *filters = "Log Files (*.log, *.txt)\0*.log;*.txt\0\0";
	char filename[_MAX_PATH] = "brickd_events.log";
	OPENFILENAME ofn = {0};
	FILE *fp;
	int count;
	LVITEM lvi_timestamp;
	char timestamp[128];
	LVITEM lvi_type;
	char type[128];
	LVITEM lvi_message;
	char message[1024];
	int i;

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = _hwnd;
	ofn.hInstance = _hinstance;
	ofn.lpstrFilter = filters;
	ofn.lpstrFile = filename;
	ofn.lpstrDefExt = "log";
	ofn.nMaxFile = sizeof(filename);
	ofn.lpstrTitle = "Save Events";
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (!GetSaveFileName(&ofn)) {
		return;
	}

	fp = fopen(filename, "wb");

	if (fp == NULL) {
		report_error("Could not write to '%s'", filename);
	}

	count = ListView_GetItemCount(_list_view);

	lvi_timestamp.iSubItem = 0;
	lvi_timestamp.mask = LVIF_TEXT;
	lvi_timestamp.pszText = timestamp;
	lvi_timestamp.cchTextMax = sizeof(timestamp) - 1;

	lvi_type.iSubItem = 1;
	lvi_type.mask = LVIF_TEXT;
	lvi_type.pszText = type;
	lvi_type.cchTextMax = sizeof(type) - 1;

	lvi_message.iSubItem = 2;
	lvi_message.mask = LVIF_TEXT;
	lvi_message.pszText = message;
	lvi_message.cchTextMax = sizeof(message) - 1;

	for (i = count - 1; i >= 0; --i) {
		lvi_timestamp.iItem = i;
		lvi_type.iItem = i;
		lvi_message.iItem = i;

		if (!ListView_GetItem(_list_view, &lvi_timestamp)) {
			strcpy(timestamp, "<unknown>");
		}

		if (!ListView_GetItem(_list_view, &lvi_type)) {
			strcpy(type, "unknown");
		}

		if (!ListView_GetItem(_list_view, &lvi_message)) {
			strcpy(message, "<unknown>");
		}

		fprintf(fp, "%s <%s> %s\r\n", timestamp, type, message);
	}

	fclose(fp);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg,
                                    WPARAM wparam, LPARAM lparam) {
	RECT client;
	MINMAXINFO *info;

	switch(msg) {
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_SIZE:
		if (_list_view != NULL) {
			GetClientRect(hwnd, &client);
			SetWindowPos(_list_view, NULL, 0, 0,
			             client.right - client.left,
			             client.bottom - client.top,
			             SWP_NOMOVE);
		}

		break;

	case WM_GETMINMAXINFO:
		info = (MINMAXINFO *)lparam;
		info->ptMinTrackSize.x = 200;
		info->ptMinTrackSize.y = 300;
		break;

	case WM_TIMER:
		read_events();
		break;

	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case ID_FILE_SAVE:
			save_events();
			break;

		case ID_FILE_EXIT:
			PostQuitMessage(0);
			break;
		}

		break;

	default:
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}

	return 0;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
	int rc;
	WNDCLASSEX wc;
	MSG msg;
	const char *class_name = "brickd_eventlog";

	(void)hPrevInstance;
	(void)lpCmdLine;

	_hinstance = hInstance;

	_event_log = OpenEventLog(NULL, "Brick Daemon");

	if (_event_log == NULL) {
		rc = GetLastError();

		report_error("Could not open event log: %s (%d)",
		             get_error_name(rc), rc);

		return 0;
	}

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
		rc = GetLastError();

		report_error("Could not register window class: %s (%d)",
		             get_error_name(rc), rc);

		CloseEventLog(_event_log);

		return 0;
	}

	_hwnd = CreateWindowEx(WS_EX_APPWINDOW | WS_EX_CLIENTEDGE,
	                       class_name,
	                       _title,
	                       WS_OVERLAPPEDWINDOW,
	                       CW_USEDEFAULT, CW_USEDEFAULT,
	                       800, 500,
	                       NULL,
	                       NULL,
	                       hInstance,
	                       NULL);

	if (_hwnd == NULL) {
		rc = GetLastError();

		report_error("Could not create window: %s (%d)",
		             get_error_name(rc), rc);

		CloseEventLog(_event_log);

		return 0;
	}

	create_menu();

	if (create_list_view() < 0) {
		CloseEventLog(_event_log);

		return 0;
	}

	ShowWindow(_hwnd, nCmdShow);
	UpdateWindow(_hwnd);

	read_events();

	SetTimer(_hwnd, 1, 200, (TIMERPROC)NULL);

	while ((rc = GetMessage(&msg, NULL, 0, 0)) != 0) {
		if (rc < 0) {
			rc = GetLastError();

			report_error("Could not get window message: %s (%d)",
			             get_error_name(rc), rc);

			break;
		} else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	free(_record_buffer);
	CloseEventLog(_event_log);

	return msg.wParam;
}
