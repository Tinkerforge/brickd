/*
 * log viewer for brickd
 * Copyright (C) 2013-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * logviewer.c: Shows event log for brickd
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
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "version.h"
#include "resources.h"

static const char *_title = "Brick Daemon - Log Viewer " VERSION_STRING;
static HINSTANCE _hinstance = NULL;
static HANDLE _event_log = NULL;
static HWND _hwnd = NULL;
static HMENU _view_menu = NULL;
static HWND _status_bar = NULL;
static HWND _event_list_view = NULL;
static HWND _debug_list_view = NULL;
static HWND _current_list_view = NULL;
static PBYTE _record_buffer = NULL;
static int _debug_connected = 0;

#define MAX_TIMESTAMP_LEN (26 + 1) // yyyy-mm-dd hh:mm:ss.uuuuuu
#define MAX_RECORD_BUFFER_SIZE 0x10000 // 64K

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
	ID_FILE_EXIT,
	ID_VIEW_EVENT,
	ID_VIEW_DEBUG
};

static void create_menu(void) {
	HMENU menu = CreateMenu();
	HMENU file_menu = CreatePopupMenu();

	_view_menu = CreatePopupMenu();

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)file_menu, "&File");
	AppendMenu(file_menu, MF_STRING, ID_FILE_SAVE, "&Save...");
	AppendMenu(file_menu, MF_STRING, ID_FILE_EXIT, "&Exit");

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)_view_menu, "&View");
	AppendMenu(_view_menu, MF_STRING, ID_VIEW_EVENT, "Windows &Event Log");
	AppendMenu(_view_menu, MF_STRING, ID_VIEW_DEBUG, "Live &Debug Log");

	SetMenu(_hwnd, menu);
}

static void set_view_menu_item_state(UINT item, UINT state) {
	MENUITEMINFO menu_item_info;

	memset(&menu_item_info, 0, sizeof(menu_item_info));

	menu_item_info.cbSize = sizeof(menu_item_info);
	menu_item_info.fMask = MIIM_STATE;
	menu_item_info.fState = state;

	SetMenuItemInfo(_view_menu, item, FALSE, &menu_item_info);
}

static void update_status_bar_message_count() {
	int count = ListView_GetItemCount(_current_list_view);
	char buffer[64];

	_snprintf(buffer, sizeof(buffer), "%d Message%s", count, count == 1 ? "" : "s");

	SendMessage(_status_bar, SB_SETTEXT, 2, (LPARAM)buffer);
}

static void update_status_bar() {
	if (_current_list_view == _event_list_view) {
		SendMessage(_status_bar, SB_SETTEXT, 0, (LPARAM)"Windows Event Log");
		SendMessage(_status_bar, SB_SETTEXT, 1, (LPARAM)"");
	} else {
		SendMessage(_status_bar, SB_SETTEXT, 0, (LPARAM)"Live Debug Log");
		SendMessage(_status_bar, SB_SETTEXT, 1, (LPARAM)(_debug_connected ? "Connected" : "Connecting..."));
	}

	update_status_bar_message_count();
}

static void set_current_list_view(HWND list_view) {
	if (_current_list_view != NULL) {
		ShowWindow(_current_list_view, SW_HIDE);
	}

	_current_list_view = list_view;

	set_view_menu_item_state(ID_VIEW_EVENT, list_view == _event_list_view ? MFS_CHECKED : MFS_UNCHECKED);
	set_view_menu_item_state(ID_VIEW_DEBUG, list_view == _debug_list_view ? MFS_CHECKED : MFS_UNCHECKED);

	ShowWindow(_current_list_view, SW_SHOW);
	SetFocus(_current_list_view);
	UpdateWindow(_current_list_view);

	update_status_bar();
}

static int init_common_controls(void) {
	INITCOMMONCONTROLSEX icex;

	icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);

	if (!InitCommonControlsEx(&icex)) {
		report_error("Could not initialize common controls");
		return -1;
	}

	return 0;
}

static int insert_list_view_column(HWND list_view, int sub_item, int width,
                                   const char *text) {
	LVCOLUMN lvc;

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.fmt = LVCFMT_LEFT;

	lvc.iSubItem = sub_item;
	lvc.cx = width;
	lvc.pszText = (char *)text;

	if (ListView_InsertColumn(list_view, sub_item, &lvc) < 0) {
		report_error("Could not insert list view column");
		return -1;
	}

	return 0;
}

enum {
	IDC_STATUSBAR = 1
};

static int create_status_bar(void) {
	int rc;
	int widths[] = {110, 240, -1};

	_status_bar = CreateWindowEx(0,
	                             STATUSCLASSNAME,
	                             NULL,
	                             WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
	                             0, 0, 0, 0,
	                             _hwnd,
	                             (HMENU)IDC_STATUSBAR,
	                             _hinstance,
	                             NULL);

	if (_status_bar == NULL) {
		rc = GetLastError();

		report_error("Could not create status bar: %s (%d)",
		             get_error_name(rc), rc);

		return -1;
	}

	SendMessage(_status_bar, SB_SETPARTS, sizeof(widths) / sizeof(int), (LPARAM)widths);

	return 0;
}

static int create_event_list_view(void) {
	RECT client_rect;
	int rc;

	GetClientRect(_hwnd, &client_rect);

	_event_list_view = CreateWindow(WC_LISTVIEW,
	                                "",
	                                WS_CHILD | LVS_REPORT |
	                                LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
	                                0, 0,
	                                client_rect.right - client_rect.left,
	                                client_rect.bottom - client_rect.top,
	                                _hwnd,
	                                NULL,
	                                _hinstance,
	                                NULL);

	if (_event_list_view == NULL) {
		rc = GetLastError();

		report_error("Could not create list view: %s (%d)",
		             get_error_name(rc), rc);

		return -1;
	}

	ListView_SetExtendedListViewStyleEx(_event_list_view,
	                                    LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

	if (insert_list_view_column(_event_list_view, 0, 120, "Timestamp") < 0 ||
	    insert_list_view_column(_event_list_view, 1,  60, "Level") < 0 ||
	    insert_list_view_column(_event_list_view, 2, 780, "Message") < 0) {
		return -1;
	}

	return 0;
}

static int create_debug_list_view(void) {
	RECT client_rect;
	int rc;

	GetClientRect(_hwnd, &client_rect);

	_debug_list_view = CreateWindow(WC_LISTVIEW,
	                                "",
	                                WS_CHILD | LVS_REPORT |
	                                LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
	                                0, 0,
	                                client_rect.right - client_rect.left,
	                                client_rect.bottom - client_rect.top,
	                                _hwnd,
	                                NULL,
	                                _hinstance,
	                                NULL);

	if (_debug_list_view == NULL) {
		rc = GetLastError();

		report_error("Could not create list view: %s (%d)",
		             get_error_name(rc), rc);

		return -1;
	}

	ListView_SetExtendedListViewStyleEx(_debug_list_view,
	                                    LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

	if (insert_list_view_column(_debug_list_view, 0, 160, "Timestamp") < 0 ||
	    insert_list_view_column(_debug_list_view, 1,  60, "Level") < 0 ||
	    insert_list_view_column(_debug_list_view, 2,  60, "Category") < 0 ||
	    insert_list_view_column(_debug_list_view, 3, 100, "File") < 0 ||
	    insert_list_view_column(_debug_list_view, 4,  35, "#") < 0 ||
	    insert_list_view_column(_debug_list_view, 5, 545, "Message") < 0) {
		return -1;
	}

	return 0;
}

static void append_event_item(const char *timestamp, const char *level,
                              const char *message) {
	SCROLLINFO si;
	LVITEM lvi;

	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;

	GetScrollInfo(_event_list_view, SB_VERT, &si);

	lvi.mask = LVIF_TEXT;

	lvi.iItem = ListView_GetItemCount(_event_list_view);
	lvi.iSubItem = 0;
	lvi.pszText = (char *)timestamp;
	ListView_InsertItem(_event_list_view, &lvi);

	lvi.iSubItem = 1;
	lvi.pszText = (char *)level;
	ListView_SetItem(_event_list_view, &lvi);

	lvi.iSubItem = 2;
	lvi.pszText = (char *)message;
	ListView_SetItem(_event_list_view, &lvi);

	if (si.nPos >= si.nMax - (int)si.nPage) {
		ListView_EnsureVisible(_event_list_view, lvi.iItem, FALSE);
	}

	update_status_bar_message_count();
}

static void append_debug_item(const char *timestamp, const char *level,
                              const char *category, const char *file,
                              const char *line, const char *message) {
	SCROLLINFO si;
	LVITEM lvi;

	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;

	GetScrollInfo(_debug_list_view, SB_VERT, &si);

	lvi.mask = LVIF_TEXT;

	lvi.iItem = ListView_GetItemCount(_debug_list_view);
	lvi.iSubItem = 0;
	lvi.pszText = (char *)timestamp;
	ListView_InsertItem(_debug_list_view, &lvi);

	lvi.iSubItem = 1;
	lvi.pszText = (char *)level;
	ListView_SetItem(_debug_list_view, &lvi);

	lvi.iSubItem = 2;
	lvi.pszText = (char *)category;
	ListView_SetItem(_debug_list_view, &lvi);

	lvi.iSubItem = 3;
	lvi.pszText = (char *)file;
	ListView_SetItem(_debug_list_view, &lvi);

	lvi.iSubItem = 4;
	lvi.pszText = (char *)line;
	ListView_SetItem(_debug_list_view, &lvi);

	lvi.iSubItem = 5;
	lvi.pszText = (char *)message;
	ListView_SetItem(_debug_list_view, &lvi);

	if (si.nPos >= si.nMax - (int)si.nPage) {
		ListView_EnsureVisible(_debug_list_view, lvi.iItem, FALSE);
	}

	update_status_bar_message_count();
}

static void format_timestamp(uint64_t seconds, int microseconds, char *buffer, int length) {
	ULONGLONG timestamp = 0;
	ULONGLONG offset_to_1970 = 116444736000000000;
	SYSTEMTIME st;
	FILETIME ft, ft_local;

	timestamp = Int32x32To64(seconds, 10000000) + offset_to_1970;
	ft.dwHighDateTime = (DWORD)((timestamp >> 32) & 0xFFFFFFFF);
	ft.dwLowDateTime = (DWORD)(timestamp & 0xFFFFFFFF);

	FileTimeToLocalFileTime(&ft, &ft_local);
	FileTimeToSystemTime(&ft_local, &st);

	if (microseconds < 0) {
		_snprintf(buffer, length, "%d-%02d-%02d %02d:%02d:%02d",
		          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	} else {
		_snprintf(buffer, length, "%d-%02d-%02d %02d:%02d:%02d:%06d",
		          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, microseconds);
	}
}

typedef enum {
	LOG_LEVEL_NONE = 0,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG
} LogLevel;

typedef enum {
	LOG_CATEGORY_EVENT = 0,
	LOG_CATEGORY_USB,
	LOG_CATEGORY_NETWORK,
	LOG_CATEGORY_HOTPLUG,
	LOG_CATEGORY_HARDWARE,
	LOG_CATEGORY_WEBSOCKET,
	LOG_CATEGORY_OTHER
} LogCategory;

#pragma pack(push)
#pragma pack(1)

typedef struct {
	uint16_t length;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	uint8_t category;
	char file[256];
	int line;
	char function[256];
	char message[1024];
} LogPipeMessage;

#pragma pack(pop)

static void append_debug_meta_message(const char *message) {
	char timestamp[MAX_TIMESTAMP_LEN];

	format_timestamp(time(NULL), 0, timestamp, sizeof(timestamp));

	append_debug_item(timestamp, "Meta", "Meta", "", "", message);
}

static void append_debug_pipe_message(LogPipeMessage *pipe_message) {
	char timestamp[MAX_TIMESTAMP_LEN];
	uint64_t seconds = pipe_message->timestamp / 1000000;
	int microseconds = pipe_message->timestamp % 1000000;
	const char *level = "<unknown>";
	const char *category = "<unknown>";
	char line[64];

	format_timestamp(seconds, microseconds, timestamp, sizeof(timestamp));

	switch (pipe_message->level) {
	case LOG_LEVEL_NONE:  level = "None";  break;
	case LOG_LEVEL_ERROR: level = "Error"; break;
	case LOG_LEVEL_WARN:  level = "Warn";  break;
	case LOG_LEVEL_INFO:  level = "Info";  break;
	case LOG_LEVEL_DEBUG: level = "Debug"; break;
	}

	switch (pipe_message->category) {
	case LOG_CATEGORY_EVENT:     category = "Event";     break;
	case LOG_CATEGORY_USB:       category = "USB";       break;
	case LOG_CATEGORY_NETWORK:   category = "Network";   break;
	case LOG_CATEGORY_HOTPLUG:   category = "Hotplug";   break;
	case LOG_CATEGORY_HARDWARE:  category = "Hardware";  break;
	case LOG_CATEGORY_WEBSOCKET: category = "WebSocket"; break;
	case LOG_CATEGORY_OTHER:     category = "Other";     break;
	}

	_snprintf(line, sizeof(line), "%d", pipe_message->line);

	append_debug_item(timestamp, level, category, pipe_message->file, line, pipe_message->message);
}

// this thread works in a fire-and-forget fashion, it's started and then just runs
static DWORD WINAPI read_named_pipe(void *opaque) {
	const char *pipe_name = "\\\\.\\pipe\\tinkerforge-brick-daemon-debug-log";
	HANDLE hpipe;
	DWORD mode = PIPE_READMODE_MESSAGE;
	LogPipeMessage pipe_message;
	DWORD bytes_read;

	(void)opaque;

	append_debug_meta_message("Connecting to Brick Daemon...");

	for (;;) {
		_debug_connected = 0;

		update_status_bar();

		for (;;) {
			hpipe = CreateFile(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

			if (hpipe != INVALID_HANDLE_VALUE) {
				break;
			}

			Sleep(250);
		}

		if (!SetNamedPipeHandleState(hpipe, &mode, NULL, NULL)) {
			CloseHandle(hpipe);

			continue;
		}

		_debug_connected = 1;

		update_status_bar();
		append_debug_meta_message("Connected to Brick Daemon");

		for (;;) {
			if (!ReadFile(hpipe, &pipe_message, sizeof(pipe_message), &bytes_read, NULL)) {
				append_debug_meta_message("Disconnected from Brick Daemon, reconnecting...");
				CloseHandle(hpipe);

				break;
			}

			if (bytes_read == sizeof(pipe_message) && pipe_message.length == sizeof(pipe_message)) {
				// enforce that strings are NUL-terminated
				pipe_message.file[sizeof(pipe_message.file) - 1] = '\0';
				pipe_message.function[sizeof(pipe_message.function) - 1] = '\0';
				pipe_message.message[sizeof(pipe_message.message) - 1] = '\0';

				append_debug_pipe_message(&pipe_message);
			}
		}
	}

	return 0;
}

static void read_event_log(void) {
	DWORD status = ERROR_SUCCESS;
	DWORD bytes_to_read = 0;
	DWORD bytes_read = 0;
	DWORD minimum_bytes_to_read = 0;
	PBYTE temp = NULL;

	if (_record_buffer == NULL) {
		bytes_to_read = MAX_RECORD_BUFFER_SIZE;
		_record_buffer = (PBYTE)malloc(bytes_to_read);

		if (_record_buffer == NULL) {
			report_error("Could not allocate record buffer");

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
					report_error("Could not reallocate record buffer to %u bytes",
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
			const char *level;
			const char *message;

			while (record < end_of_records) {
				if (strcmp((const char *)(record + sizeof(EVENTLOGRECORD)), "Brick Daemon") == 0) {
					format_timestamp(((PEVENTLOGRECORD)record)->TimeGenerated, -1,
					                 timestamp, sizeof(timestamp));

					switch (((PEVENTLOGRECORD)record)->EventType) {
					case EVENTLOG_ERROR_TYPE:
						level = "Error";
						break;

					case EVENTLOG_WARNING_TYPE:
						level = "Warn";
						break;

					case EVENTLOG_INFORMATION_TYPE:
						level = "Info";
						break;

					case EVENTLOG_AUDIT_SUCCESS:
						level = "Audit Success";
						break;

					case EVENTLOG_AUDIT_FAILURE:
						level = "Audit Failure";
						break;

					default:
						level = "<unknown>";
						break;
					}

					if (((PEVENTLOGRECORD)record)->NumStrings > 0) {
						message = (const char *)(record + ((PEVENTLOGRECORD)record)->StringOffset);
					} else {
						message = "<unknown>";
					}

					append_event_item(timestamp, level, message);
				}

				record += ((PEVENTLOGRECORD)record)->Length;
			}
		}
	}
}

static void save_event_log(void) {
	char *filters = "Log Files (*.log, *.txt)\0*.log;*.txt\0\0";
	char filename[_MAX_PATH] = "brickd_event.log";
	OPENFILENAME ofn = {0};
	FILE *fp;
	int count;
	LVITEM lvi_timestamp;
	char timestamp[128];
	LVITEM lvi_level;
	char level[64];
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
	ofn.lpstrTitle = "Save Windows Event Log";
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (!GetSaveFileName(&ofn)) {
		return;
	}

	fp = fopen(filename, "wb");

	if (fp == NULL) {
		report_error("Could not write to '%s'", filename);
	}

	count = ListView_GetItemCount(_event_list_view);

	lvi_timestamp.iSubItem = 0;
	lvi_timestamp.mask = LVIF_TEXT;
	lvi_timestamp.pszText = timestamp;
	lvi_timestamp.cchTextMax = sizeof(timestamp) - 1;

	lvi_level.iSubItem = 1;
	lvi_level.mask = LVIF_TEXT;
	lvi_level.pszText = level;
	lvi_level.cchTextMax = sizeof(level) - 1;

	lvi_message.iSubItem = 2;
	lvi_message.mask = LVIF_TEXT;
	lvi_message.pszText = message;
	lvi_message.cchTextMax = sizeof(message) - 1;

	for (i = 0; i < count; ++i) {
		lvi_timestamp.iItem = i;
		lvi_level.iItem = i;
		lvi_message.iItem = i;

		if (!ListView_GetItem(_event_list_view, &lvi_timestamp)) {
			strcpy(timestamp, "<unknown>");
		}

		if (!ListView_GetItem(_event_list_view, &lvi_level)) {
			strcpy(level, "unknown");
		}

		if (!ListView_GetItem(_event_list_view, &lvi_message)) {
			strcpy(message, "<unknown>");
		}

		fprintf(fp, "%s <%s> %s\r\n", timestamp, level, message);
	}

	fclose(fp);
}

static void save_debug_log(void) {
	char *filters = "Log Files (*.log, *.txt)\0*.log;*.txt\0\0";
	char filename[_MAX_PATH] = "brickd_debug.log";
	OPENFILENAME ofn = {0};
	FILE *fp;
	int count;
	LVITEM lvi_timestamp;
	char timestamp[128];
	LVITEM lvi_level;
	char level[64];
	LVITEM lvi_category;
	char category[64];
	LVITEM lvi_file;
	char file[256];
	LVITEM lvi_line;
	char line[64];
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
	ofn.lpstrTitle = "Save Live Debug Log";
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (!GetSaveFileName(&ofn)) {
		return;
	}

	fp = fopen(filename, "wb");

	if (fp == NULL) {
		report_error("Could not write to '%s'", filename);
	}

	count = ListView_GetItemCount(_debug_list_view);

	lvi_timestamp.iSubItem = 0;
	lvi_timestamp.mask = LVIF_TEXT;
	lvi_timestamp.pszText = timestamp;
	lvi_timestamp.cchTextMax = sizeof(timestamp) - 1;

	lvi_level.iSubItem = 1;
	lvi_level.mask = LVIF_TEXT;
	lvi_level.pszText = level;
	lvi_level.cchTextMax = sizeof(level) - 1;

	lvi_category.iSubItem = 2;
	lvi_category.mask = LVIF_TEXT;
	lvi_category.pszText = category;
	lvi_category.cchTextMax = sizeof(category) - 1;

	lvi_file.iSubItem = 3;
	lvi_file.mask = LVIF_TEXT;
	lvi_file.pszText = file;
	lvi_file.cchTextMax = sizeof(file) - 1;

	lvi_line.iSubItem = 4;
	lvi_line.mask = LVIF_TEXT;
	lvi_line.pszText = line;
	lvi_line.cchTextMax = sizeof(line) - 1;

	lvi_message.iSubItem = 5;
	lvi_message.mask = LVIF_TEXT;
	lvi_message.pszText = message;
	lvi_message.cchTextMax = sizeof(message) - 1;

	for (i = 0; i < count; ++i) {
		lvi_timestamp.iItem = i;
		lvi_level.iItem = i;
		lvi_category.iItem = i;
		lvi_file.iItem = i;
		lvi_line.iItem = i;
		lvi_message.iItem = i;

		if (!ListView_GetItem(_debug_list_view, &lvi_timestamp)) {
			strcpy(timestamp, "<unknown>");
		}

		if (!ListView_GetItem(_debug_list_view, &lvi_level)) {
			strcpy(level, "unknown");
		}

		if (!ListView_GetItem(_debug_list_view, &lvi_category)) {
			strcpy(category, "unknown");
		}

		if (!ListView_GetItem(_debug_list_view, &lvi_file)) {
			strcpy(file, "unknown");
		}

		if (!ListView_GetItem(_debug_list_view, &lvi_line)) {
			strcpy(line, "unknown");
		}

		if (!ListView_GetItem(_debug_list_view, &lvi_message)) {
			strcpy(message, "<unknown>");
		}

		fprintf(fp, "%s <%s> <%s|%s:%s> %s\r\n", timestamp, level, category, file, line, message);
	}

	fclose(fp);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg,
                                    WPARAM wparam, LPARAM lparam) {
	RECT client_rect;
	RECT status_bar_rect;
	MINMAXINFO *info;

	switch(msg) {
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_SIZE:
		GetClientRect(hwnd, &client_rect);
		SendMessage(_status_bar, WM_SIZE, 0, 0);
		GetWindowRect(_status_bar, &status_bar_rect);
		client_rect.bottom -= status_bar_rect.bottom - status_bar_rect.top;

		if (_event_list_view != NULL) {
			SetWindowPos(_event_list_view, NULL, 0, 0,
			             client_rect.right - client_rect.left,
			             client_rect.bottom - client_rect.top,
			             SWP_NOMOVE);
		}

		if (_debug_list_view != NULL) {
			SetWindowPos(_debug_list_view, NULL, 0, 0,
			             client_rect.right - client_rect.left,
			             client_rect.bottom - client_rect.top,
			             SWP_NOMOVE);
		}

		break;

	case WM_GETMINMAXINFO:
		info = (MINMAXINFO *)lparam;
		info->ptMinTrackSize.x = 400;
		info->ptMinTrackSize.y = 300;
		break;

	case WM_TIMER:
		read_event_log();
		break;

	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case ID_FILE_SAVE:
			if (_current_list_view == _event_list_view) {
				save_event_log();
			} else {
				save_debug_log();
			}

			break;

		case ID_FILE_EXIT:
			PostQuitMessage(0);
			break;

		case ID_VIEW_EVENT:
			set_current_list_view(_event_list_view);
			break;

		case ID_VIEW_DEBUG:
			set_current_list_view(_debug_list_view);
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
	const char *class_name = "brickd_logviewer";

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
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_32));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_16));

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
	                       1000, 700,
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

	if (init_common_controls() < 0 ||
	    create_status_bar() < 0 ||
	    create_event_list_view() < 0 ||
	    create_debug_list_view() < 0) {
		CloseEventLog(_event_log);

		return 0;
	}

	set_current_list_view(_event_list_view);

	ShowWindow(_hwnd, nCmdShow);
	UpdateWindow(_hwnd);

	if (CreateThread(NULL, 0, read_named_pipe, NULL, 0, NULL) == NULL) {
		rc = GetLastError();

		report_error("Could not create named pipe thread: %s (%d)",
		             get_error_name(rc), rc);
	}

	read_event_log();

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
