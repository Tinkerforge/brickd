/*
 * log viewer for brickd
 * Copyright (C) 2013-2015, 2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * logviewer.c: Shows brickd log file
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
#include <shlobj.h>
#include <shellapi.h>

#include "../../../brickd/version.h"
#include "resources.h"

#define MAX_TIMESTAMP_LENGTH (26 + 1) // yyyy-mm-dd hh:mm:ss.uuuuuu
#define MAX_LIVE_LOG_QUEUE_LENGTH 10000
#define MAX_LIVE_LOG_VIEW_LENGTH 5000
#define DELTA_EPOCH 116444736000000000ULL // difference between Unix epoch and January 1, 1601 in 100-nanoseconds

typedef enum {
	LOG_LEVEL_ERROR = 0,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG
} LogLevel;

enum {
	ID_BRICK_DAEMON_RESTART,
	ID_BRICK_DAEMON_STOP,
	ID_BRICK_DAEMON_EXIT,
	ID_LIVE_LOG_ERROR_LEVEL,
	ID_LIVE_LOG_WARN_LEVEL,
	ID_LIVE_LOG_INFO_LEVEL,
	ID_LIVE_LOG_DEBUG_LEVEL,
	ID_LIVE_LOG_PAUSE,
	ID_LIVE_LOG_SAVE,
	ID_LOG_FILE_VIEW_FILE,
	ID_LOG_FILE_VIEW_DIRECTORY,
	ID_CONFIG_FILE_EDIT_FILE,
	ID_CONFIG_FILE_VIEW_FILE,
	ID_CONFIG_FILE_VIEW_DIRECTORY,
	ID_CONFIG_FILE_VIEW_DEFAULT
};

enum {
	IDC_STATUSBAR = 1
};

typedef struct {
	char timestamp[MAX_TIMESTAMP_LENGTH];
	char level[16];
	char source[192];
	char message[1024];
} LiveLogItem;

typedef enum { // bitmask
	LOG_PIPE_MESSAGE_FLAG_LIBUSB = 0x0001
} LogPipeMessageFlag;

#pragma pack(push)
#pragma pack(1)

typedef struct {
	uint16_t length;
	uint8_t flags;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	char source[128];
	int line;
	char message[1024];
} LogPipeMessage;

#pragma pack(pop)

typedef void (WINAPI *GETSYSTEMTIMEPRECISEASFILETIME)(LPFILETIME);

static GETSYSTEMTIMEPRECISEASFILETIME ptr_GetSystemTimePreciseAsFileTime = NULL;

static char _executable_filename[MAX_PATH];
static char _executable_directory[MAX_PATH];
static char _program_data_directory[MAX_PATH];
static char _log_filename[MAX_PATH];
static char _config_filename[MAX_PATH];
static char _config_default_filename[MAX_PATH];
static const char *_title = "Brick Daemon " VERSION_STRING " - Log Viewer";
static HINSTANCE _hinstance = NULL;
static HWND _hwnd = NULL;
static HMENU _live_log_menu = NULL;
static HWND _status_bar = NULL;
static HWND _live_log_view = NULL;
static LogLevel _live_log_level = LOG_LEVEL_INFO;
static int _live_log_paused = 0;
static int _live_log_saving = 0;
static CRITICAL_SECTION _live_log_dropped_lock;
static unsigned int _live_log_dropped = 0; // protected by _live_log_dropped_lock
static unsigned int _live_log_last_dropped = 0;
static int _live_log_connected = 0;
static CRITICAL_SECTION _live_log_queue_lock;
static LiveLogItem *_live_log_queue_items[2] = {NULL, NULL}; // protected by _live_log_queue_lock
static int _live_log_queue_used[2] = {0, 0}; // protected by _live_log_queue_lock

static const char *_pipe_names[] = {
	"\\\\.\\pipe\\tinkerforge-brick-daemon-error-log",
	"\\\\.\\pipe\\tinkerforge-brick-daemon-warn-log",
	"\\\\.\\pipe\\tinkerforge-brick-daemon-info-log",
	"\\\\.\\pipe\\tinkerforge-brick-daemon-debug-log",
};

static void string_copy(char *target, int target_length,
                 const char *source, int source_length) {
	int copy_length;

	if (target_length <= 0) {
		return;
	}

	if (source_length >= 0 && source_length < target_length - 1) {
		copy_length = source_length;
	} else {
		copy_length = target_length - 1;
	}

	strncpy(target, source, copy_length);

	target[copy_length] = '\0';
}

static void string_append(char *target, int target_length, const char *source) {
	int offset;

	if (target_length <= 0) {
		return;
	}

	offset = strlen(target);

	if (offset >= target_length - 1) {
		return;
	}

	strncpy(target + offset, source, target_length - offset - 1);

	target[target_length - 1] = '\0';
}

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
	ERROR_NAME(ERROR_PIPE_BUSY);

	// FIXME

	default: return "<unknown>";
	}
}

static void microtime(uint64_t *seconds, int *microseconds) {
	FILETIME ft;
	uint64_t t;

	if (ptr_GetSystemTimePreciseAsFileTime != NULL) {
		ptr_GetSystemTimePreciseAsFileTime(&ft);
	} else {
		GetSystemTimeAsFileTime(&ft);
	}

	t = ((unsigned long long)ft.dwHighDateTime << 32) | (unsigned long long)ft.dwLowDateTime;
	t = (t - DELTA_EPOCH) / 10; // 100-nanoseconds to microseconds

	*seconds = t / 1000000UL;
	*microseconds = (int)(t % 1000000UL);
}

static void set_menu_item_type(HMENU menu, unsigned int item, unsigned int type) {
	MENUITEMINFO menu_item_info;

	memset(&menu_item_info, 0, sizeof(menu_item_info));

	menu_item_info.cbSize = sizeof(menu_item_info);
	menu_item_info.fMask = MIIM_FTYPE;
	menu_item_info.fType = MFT_STRING | type;

	SetMenuItemInfo(menu, item, FALSE, &menu_item_info);
}

static void create_menu() {
	HMENU menu = CreateMenu();
	HMENU brick_daemon_menu = CreatePopupMenu();
	HMENU log_file_menu = CreatePopupMenu();
	HMENU config_file_menu = CreatePopupMenu();

	_live_log_menu = CreatePopupMenu();

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)brick_daemon_menu, "&Brick Daemon");
	AppendMenu(brick_daemon_menu, MF_STRING, ID_BRICK_DAEMON_RESTART, "(&Re)start Brick Daemon");
	AppendMenu(brick_daemon_menu, MF_STRING, ID_BRICK_DAEMON_STOP, "S&top Brick Daemon");
	AppendMenu(brick_daemon_menu, MF_SEPARATOR, 0, "");
	AppendMenu(brick_daemon_menu, MF_STRING, ID_BRICK_DAEMON_EXIT, "&Exit Log Viewer");

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)_live_log_menu, "&Live Log");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_ERROR_LEVEL, "&Error Level");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_WARN_LEVEL, "&Warn Level");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_INFO_LEVEL, "&Info Level");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_DEBUG_LEVEL, "&Debug Level");
	AppendMenu(_live_log_menu, MF_SEPARATOR, 0, "");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_PAUSE, "&Pause");
	AppendMenu(_live_log_menu, MF_SEPARATOR, 0, "");
	AppendMenu(_live_log_menu, MF_STRING, ID_LIVE_LOG_SAVE, "&Save...");

	set_menu_item_type(_live_log_menu, ID_LIVE_LOG_ERROR_LEVEL, MFT_RADIOCHECK);
	set_menu_item_type(_live_log_menu, ID_LIVE_LOG_WARN_LEVEL, MFT_RADIOCHECK);
	set_menu_item_type(_live_log_menu, ID_LIVE_LOG_INFO_LEVEL, MFT_RADIOCHECK);
	set_menu_item_type(_live_log_menu, ID_LIVE_LOG_DEBUG_LEVEL, MFT_RADIOCHECK);

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)log_file_menu, "L&og File");
	AppendMenu(log_file_menu, MF_STRING, ID_LOG_FILE_VIEW_FILE, "View &Log File (read-only)");
	AppendMenu(log_file_menu, MF_STRING, ID_LOG_FILE_VIEW_DIRECTORY, "View Log &Directory");

	AppendMenu(menu, MF_STRING | MF_POPUP, (UINT)config_file_menu, "&Config File");
	AppendMenu(config_file_menu, MF_STRING, ID_CONFIG_FILE_EDIT_FILE, "&Edit Config File");
	AppendMenu(config_file_menu, MF_STRING, ID_CONFIG_FILE_VIEW_FILE, "&View Config File (read-only)");
	AppendMenu(config_file_menu, MF_STRING, ID_CONFIG_FILE_VIEW_DIRECTORY, "View Config D&irectory");
	AppendMenu(config_file_menu, MF_STRING, ID_CONFIG_FILE_VIEW_DEFAULT, "View &Default Config File (read-only)");

	SetMenu(_hwnd, menu);
}

static void set_live_log_menu_item_state(UINT item, UINT state) {
	MENUITEMINFO menu_item_info;

	memset(&menu_item_info, 0, sizeof(menu_item_info));

	menu_item_info.cbSize = sizeof(menu_item_info);
	menu_item_info.fMask = MIIM_STATE;
	menu_item_info.fState = state;

	SetMenuItemInfo(_live_log_menu, item, FALSE, &menu_item_info);
}

static void update_status_bar() {
	const char *log_name = "Unknown";
	int message_count = ListView_GetItemCount(_live_log_view);
	unsigned int dropped;
	char buffer[128];

	switch (_live_log_level) {
	case LOG_LEVEL_ERROR: log_name = "Live Error Log"; break;
	case LOG_LEVEL_WARN:  log_name = "Live Warn Log";  break;
	case LOG_LEVEL_INFO:  log_name = "Live Info Log";  break;
	case LOG_LEVEL_DEBUG: log_name = "Live Debug Log"; break;
	}

	_snprintf(buffer, sizeof(buffer), _live_log_saving ? "%s [Saving]" : (_live_log_paused ? "%s [Paused]" : "%s"), log_name);
	SendMessage(_status_bar, SB_SETTEXT, 0, (LPARAM)buffer);

	SendMessage(_status_bar, SB_SETTEXT, 1, (LPARAM)(_live_log_connected ? "Connected" : "Connecting..."));

	EnterCriticalSection(&_live_log_dropped_lock);
	dropped = _live_log_dropped;
	LeaveCriticalSection(&_live_log_dropped_lock);

	if (dropped == 0) {
		_snprintf(buffer, sizeof(buffer), "%d Message%s", message_count, message_count == 1 ? "" : "s");
	} else {
		_snprintf(buffer, sizeof(buffer), "Last %d of %d Message%s", message_count, dropped + message_count, message_count == 1 ? "" : "s");
	}

	SendMessage(_status_bar, SB_SETTEXT, 2, (LPARAM)buffer);
}

static void set_live_log_level(LogLevel level) {
	_live_log_level = level;

	set_live_log_menu_item_state(ID_LIVE_LOG_ERROR_LEVEL, MFS_UNCHECKED);
	set_live_log_menu_item_state(ID_LIVE_LOG_WARN_LEVEL, MFS_UNCHECKED);
	set_live_log_menu_item_state(ID_LIVE_LOG_INFO_LEVEL, MFS_UNCHECKED);
	set_live_log_menu_item_state(ID_LIVE_LOG_DEBUG_LEVEL, MFS_UNCHECKED);

	switch (level) {
	case LOG_LEVEL_ERROR: set_live_log_menu_item_state(ID_LIVE_LOG_ERROR_LEVEL, MFS_CHECKED); break;
	case LOG_LEVEL_WARN:  set_live_log_menu_item_state(ID_LIVE_LOG_WARN_LEVEL, MFS_CHECKED);  break;
	case LOG_LEVEL_INFO:  set_live_log_menu_item_state(ID_LIVE_LOG_INFO_LEVEL, MFS_CHECKED);  break;
	case LOG_LEVEL_DEBUG: set_live_log_menu_item_state(ID_LIVE_LOG_DEBUG_LEVEL, MFS_CHECKED); break;
	}

	update_status_bar();
}

static void set_live_log_paused(int paused) {
	_live_log_paused = paused;

	set_live_log_menu_item_state(ID_LIVE_LOG_PAUSE, _live_log_paused ? MFS_CHECKED : MFS_UNCHECKED);

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

static int create_status_bar() {
	int rc;
	int widths[] = {175, 350, -1};

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

	SendMessage(_status_bar, SB_SETPARTS,
	            sizeof(widths) / sizeof(int), (LPARAM)widths);

	return 0;
}

static int create_live_log_view() {
	RECT client_rect;
	int rc;

	GetClientRect(_hwnd, &client_rect);

	_live_log_view = CreateWindow(WC_LISTVIEW,
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

	if (_live_log_view == NULL) {
		rc = GetLastError();

		report_error("Could not create list view: %s (%d)",
		             get_error_name(rc), rc);

		return -1;
	}

	ListView_SetExtendedListViewStyleEx(_live_log_view,
	                                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER,
	                                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	if (insert_list_view_column(_live_log_view, 0, 160, "Timestamp") < 0 ||
	    insert_list_view_column(_live_log_view, 1,  60, "Level") < 0 ||
	    insert_list_view_column(_live_log_view, 2, 180, "Source") < 0 ||
	    insert_list_view_column(_live_log_view, 3, 560, "Message") < 0) {
		return -1;
	}

	return 0;
}

static void queue_live_log_item(const char *timestamp, const char *level,
                                const char *source, const char *message) {
	LiveLogItem *item;
	int dropped = 0;

	EnterCriticalSection(&_live_log_queue_lock);

	if (_live_log_queue_used[0] + 1 < MAX_LIVE_LOG_QUEUE_LENGTH) {
		item = &_live_log_queue_items[0][_live_log_queue_used[0]++];

		string_copy(item->timestamp, sizeof(item->timestamp), timestamp, -1);
		string_copy(item->level, sizeof(item->level), level, -1);
		string_copy(item->source, sizeof(item->source), source, -1);
		string_copy(item->message, sizeof(item->message), message, -1);
	} else {
		dropped = 1;
	}

	LeaveCriticalSection(&_live_log_queue_lock);

	if (dropped) {
		EnterCriticalSection(&_live_log_dropped_lock);
		++_live_log_dropped;
		LeaveCriticalSection(&_live_log_dropped_lock);
	}
}

static void format_timestamp(uint64_t seconds, int microseconds,
                             char *buffer, int length, char *date_separator,
                             char *date_time_separator, char *time_separator) {
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
		_snprintf(buffer, length, "%d%s%02d%s%02d%s%02d%s%02d%s%02d",
		          st.wYear, date_separator, st.wMonth, date_separator,
		          st.wDay, date_time_separator, st.wHour, time_separator,
		          st.wMinute, time_separator, st.wSecond);
	} else {
		_snprintf(buffer, length, "%d%s%02d%s%02d%s%02d%s%02d%s%02d.%06d",
		          st.wYear, date_separator, st.wMonth, date_separator,
		          st.wDay, date_time_separator, st.wHour, time_separator,
		          st.wMinute, time_separator, st.wSecond, microseconds);
	}
}

static void queue_live_log_meta_message(const char *format, ...) {
	char timestamp[MAX_TIMESTAMP_LENGTH];
	uint64_t seconds;
	int microseconds;
	char message[1024 + 1];
	va_list arguments;

	microtime(&seconds, &microseconds);
	format_timestamp(seconds, microseconds, timestamp, sizeof(timestamp), "-", " ", ":");

	va_start(arguments, format);
	_vsnprintf_s(message, sizeof(message), sizeof(message) - 1, format, arguments);
	va_end(arguments);

	queue_live_log_item(timestamp, "Meta", "Log Viewer", message);
}

static void queue_live_log_pipe_message(LogPipeMessage *message) {
	char timestamp[MAX_TIMESTAMP_LENGTH];
	uint64_t seconds = message->timestamp / 1000000;
	int microseconds = message->timestamp % 1000000;
	const char *level = "Unknown";
	char source[192];

	format_timestamp(seconds, microseconds, timestamp, sizeof(timestamp), "-", " ", ":");

	switch (message->level) {
	case LOG_LEVEL_ERROR: level = "Error"; break;
	case LOG_LEVEL_WARN:  level = "Warn";  break;
	case LOG_LEVEL_INFO:  level = "Info";  break;
	case LOG_LEVEL_DEBUG: level = "Debug"; break;
	}

	if ((message->flags & LOG_PIPE_MESSAGE_FLAG_LIBUSB) != 0) {
		_snprintf(source, sizeof(source), "libusb:%s", message->source);
	} else {
		_snprintf(source, sizeof(source), "%s:%d", message->source, message->line);
	}

	queue_live_log_item(timestamp, level, source, message->message);
}

// this thread works in a fire-and-forget fashion, it's started and then just runs
static DWORD WINAPI read_live_log(void *opaque) {
	HANDLE overlapped_event;
	HANDLE hpipe;
	LogLevel current_live_log_level;
	DWORD current_error;
	DWORD last_error;
	OVERLAPPED overlapped;
	LogPipeMessage message;
	DWORD bytes_read;
	int live_log_level_changed = 0;
	DWORD rc;

	(void)opaque;

	overlapped_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (overlapped_event == NULL) {
		current_error = GetLastError();

		queue_live_log_meta_message("Count not create event for async read: %s (%d)",
		                            get_error_name(current_error), current_error);

		return 0;
	}

	queue_live_log_meta_message("Connecting to Brick Daemon...");

	for (;;) {
		_live_log_connected = 0;
		live_log_level_changed = 0;
		last_error = ERROR_SUCCESS;

		update_status_bar();

		for (;;) {
			current_live_log_level = _live_log_level;

			hpipe = CreateFile(_pipe_names[current_live_log_level], GENERIC_READ,
			                   0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

			if (hpipe != INVALID_HANDLE_VALUE) {
				break;
			}

			current_error = GetLastError();

			if (current_error != last_error && current_error != ERROR_FILE_NOT_FOUND) {
				queue_live_log_meta_message("Error while connecting to Brick Daemon, trying again: %s (%d)",
				                            get_error_name(current_error), current_error);
			}

			last_error = current_error;

			Sleep(500);
		}

		_live_log_connected = 1;

		update_status_bar();
		queue_live_log_meta_message("Connected to Brick Daemon");

		for (;;) {
			memset(&overlapped, 0, sizeof(overlapped));
			overlapped.hEvent = overlapped_event;

			if (!ReadFile(hpipe, &message, sizeof(message), NULL, &overlapped)) {
				current_error = GetLastError();

				if (current_error != ERROR_IO_PENDING) {
					if (current_error == ERROR_BROKEN_PIPE) {
						queue_live_log_meta_message("Disconnected from Brick Daemon, reconnecting...");
					} else {
						queue_live_log_meta_message("Disconnected from Brick Daemon, reconnecting: %s (%d)",
						                            get_error_name(current_error), current_error);
					}

					CloseHandle(hpipe);

					break;
				}
			}

			for (;;) {
				current_error = ERROR_SUCCESS;

				if (current_live_log_level != _live_log_level) {
					current_live_log_level = _live_log_level;
					live_log_level_changed = 1;

					queue_live_log_meta_message("Live Log Level changed, reconnecting...");

					CancelIo(hpipe);
				}

				rc = WaitForSingleObject(overlapped_event, 100);

				if (rc != WAIT_OBJECT_0 && rc != WAIT_TIMEOUT) {
					CancelIo(hpipe);
				}

				if (GetOverlappedResult(hpipe, &overlapped, &bytes_read, FALSE)) {
					break;
				}

				current_error = GetLastError();

				if (current_error == ERROR_IO_INCOMPLETE) {
					continue;
				}

				break;
			}

			if (current_error != ERROR_SUCCESS) {
				if (current_error == ERROR_BROKEN_PIPE) {
					queue_live_log_meta_message("Disconnected from Brick Daemon, reconnecting...");
				} else if (current_error != ERROR_OPERATION_ABORTED) {
					queue_live_log_meta_message("Error while reading from Brick Daemon, reconnecting: %s (%d)",
					                            get_error_name(current_error), current_error);
				}

				CloseHandle(hpipe);

				break;
			}

			if (live_log_level_changed) {
				CloseHandle(hpipe);

				break;
			}

			if (bytes_read == sizeof(message) && message.length == sizeof(message)) {
				if (!_live_log_paused && !_live_log_saving) {
					// enforce that strings are NUL-terminated
					message.source[sizeof(message.source) - 1] = '\0';
					message.message[sizeof(message.message) - 1] = '\0';

					queue_live_log_pipe_message(&message);
				} else {
					EnterCriticalSection(&_live_log_dropped_lock);
					++_live_log_dropped;
					LeaveCriticalSection(&_live_log_dropped_lock);
				}
			}
		}
	}
}

static void update_live_log_view(void) {
	int i;
	int offset;
	unsigned int dropped;
	LiveLogItem *item;
	SCROLLINFO si;
	LVITEM lvi;

	EnterCriticalSection(&_live_log_queue_lock);

	item = _live_log_queue_items[0];
	_live_log_queue_items[0] = _live_log_queue_items[1];
	_live_log_queue_items[1] = item;

	i = _live_log_queue_used[0];
	_live_log_queue_used[0] = _live_log_queue_used[1];
	_live_log_queue_used[1] = i;

	LeaveCriticalSection(&_live_log_queue_lock);

	if (_live_log_queue_used[1] == 0) {
		EnterCriticalSection(&_live_log_dropped_lock);
		dropped = _live_log_dropped;
		LeaveCriticalSection(&_live_log_dropped_lock);

		if (_live_log_last_dropped != dropped) {
			_live_log_last_dropped = dropped;

			update_status_bar();
		}

		return;
	}

	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;

	lvi.mask = LVIF_TEXT;

	GetScrollInfo(_live_log_view, SB_VERT, &si);
	SendMessage(_live_log_view, WM_SETREDRAW, (WPARAM)FALSE, 0);

	if (_live_log_queue_used[1] >= MAX_LIVE_LOG_VIEW_LENGTH) {
		offset = _live_log_queue_used[1] - MAX_LIVE_LOG_VIEW_LENGTH;

		EnterCriticalSection(&_live_log_dropped_lock);
		_live_log_dropped += ListView_GetItemCount(_live_log_view);
		LeaveCriticalSection(&_live_log_dropped_lock);

		ListView_DeleteAllItems(_live_log_view);
	} else {
		offset = 0;
		dropped = 0;

		while (ListView_GetItemCount(_live_log_view) > MAX_LIVE_LOG_VIEW_LENGTH - _live_log_queue_used[1]) {
			++dropped;

			ListView_DeleteItem(_live_log_view, 0);
		}

		EnterCriticalSection(&_live_log_dropped_lock);
		_live_log_dropped += dropped;
		LeaveCriticalSection(&_live_log_dropped_lock);
	}

	EnterCriticalSection(&_live_log_dropped_lock);
	_live_log_dropped += offset;
	LeaveCriticalSection(&_live_log_dropped_lock);

	for (i = offset; i < _live_log_queue_used[1]; ++i) {
		item = &_live_log_queue_items[1][i];

		lvi.iItem = ListView_GetItemCount(_live_log_view);
		lvi.iSubItem = 0;
		lvi.pszText = (char *)item->timestamp;
		ListView_InsertItem(_live_log_view, &lvi);

		lvi.iSubItem = 1;
		lvi.pszText = (char *)item->level;
		ListView_SetItem(_live_log_view, &lvi);

		lvi.iSubItem = 2;
		lvi.pszText = (char *)item->source;
		ListView_SetItem(_live_log_view, &lvi);

		lvi.iSubItem = 3;
		lvi.pszText = (char *)item->message;
		ListView_SetItem(_live_log_view, &lvi);
	}

	_live_log_queue_used[1] = 0;

	//if (si.nPos >= si.nMax - (int)si.nPage) {
		ListView_EnsureVisible(_live_log_view, ListView_GetItemCount(_live_log_view) - 1, FALSE);
	//}

	SendMessage(_live_log_view, WM_SETREDRAW, (WPARAM)TRUE, 0);

	update_status_bar();
}

static void save_live_log(void) {
	char save_timestamp[MAX_TIMESTAMP_LENGTH];
	char save_filename[_MAX_PATH];
	char *filters = "Log Files (*.log, *.txt)\0*.log;*.txt\0\0";
	OPENFILENAME ofn;
	FILE *fp;
	int count;
	LVITEM lvi_timestamp;
	char timestamp[128];
	LVITEM lvi_level;
	char level[64];
	LVITEM lvi_source;
	char source[192];
	LVITEM lvi_message;
	char message[1024];
	int i;

	_live_log_saving = 1;

	update_status_bar();

	format_timestamp(time(NULL), -1, save_timestamp, sizeof(save_timestamp), "", "_", "");
	_snprintf(save_filename, sizeof(save_filename), "brickd_live_%s.log", save_timestamp);

	memset(&ofn, 0, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = _hwnd;
	ofn.hInstance = _hinstance;
	ofn.lpstrFilter = filters;
	ofn.lpstrFile = save_filename;
	ofn.lpstrDefExt = "log";
	ofn.nMaxFile = sizeof(save_filename);
	ofn.lpstrTitle = "Save Live Log";
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (!GetSaveFileName(&ofn)) {
		_live_log_saving = 0;

		update_status_bar();

		return;
	}

	fp = fopen(save_filename, "wb");

	if (fp == NULL) {
		_live_log_saving = 0;

		update_status_bar();
		report_error("Could not write to '%s'", save_filename);

		return;
	}

	count = ListView_GetItemCount(_live_log_view);

	lvi_timestamp.iSubItem = 0;
	lvi_timestamp.mask = LVIF_TEXT;
	lvi_timestamp.pszText = timestamp;
	lvi_timestamp.cchTextMax = sizeof(timestamp) - 1;

	lvi_level.iSubItem = 1;
	lvi_level.mask = LVIF_TEXT;
	lvi_level.pszText = level;
	lvi_level.cchTextMax = sizeof(level) - 1;

	lvi_source.iSubItem = 2;
	lvi_source.mask = LVIF_TEXT;
	lvi_source.pszText = source;
	lvi_source.cchTextMax = sizeof(source) - 1;

	lvi_message.iSubItem = 3;
	lvi_message.mask = LVIF_TEXT;
	lvi_message.pszText = message;
	lvi_message.cchTextMax = sizeof(message) - 1;

	for (i = 0; i < count; ++i) {
		lvi_timestamp.iItem = i;
		lvi_level.iItem = i;
		lvi_source.iItem = i;
		lvi_message.iItem = i;

		if (!ListView_GetItem(_live_log_view, &lvi_timestamp)) {
			strcpy(timestamp, "Unknown");
		}

		if (!ListView_GetItem(_live_log_view, &lvi_level)) {
			strcpy(level, "Unknown");
		}

		if (!ListView_GetItem(_live_log_view, &lvi_source)) {
			strcpy(source, "Unknown");
		}

		if (!ListView_GetItem(_live_log_view, &lvi_message)) {
			strcpy(message, "Unknown");
		}

		fprintf(fp, "%s <%s> [%s] %s\r\n", timestamp, level, source, message);
	}

	fclose(fp);

	_live_log_saving = 0;

	update_status_bar();
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

		if (_live_log_view != NULL) {
			SetWindowPos(_live_log_view, NULL, 0, 0,
			             client_rect.right - client_rect.left,
			             client_rect.bottom - client_rect.top,
			             SWP_NOMOVE);
		}

		break;

	case WM_GETMINMAXINFO:
		info = (MINMAXINFO *)lparam;
		info->ptMinTrackSize.x = 525;
		info->ptMinTrackSize.y = 400;
		break;

	case WM_TIMER:
		update_live_log_view();
		break;

	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case ID_BRICK_DAEMON_RESTART:
			ShellExecuteA(hwnd, "runas", _executable_filename, "brickd-restart", NULL, SW_HIDE);
			break;

		case ID_BRICK_DAEMON_STOP:
			ShellExecuteA(hwnd, "runas", _executable_filename, "brickd-stop", NULL, SW_HIDE);
			break;

		case ID_BRICK_DAEMON_EXIT:
			PostQuitMessage(0);
			break;

		case ID_LIVE_LOG_ERROR_LEVEL:
			set_live_log_level(LOG_LEVEL_ERROR);
			break;

		case ID_LIVE_LOG_WARN_LEVEL:
			set_live_log_level(LOG_LEVEL_WARN);
			break;

		case ID_LIVE_LOG_INFO_LEVEL:
			set_live_log_level(LOG_LEVEL_INFO);
			break;

		case ID_LIVE_LOG_DEBUG_LEVEL:
			set_live_log_level(LOG_LEVEL_DEBUG);
			break;

		case ID_LIVE_LOG_PAUSE:
			set_live_log_paused(!_live_log_paused);
			break;

		case ID_LIVE_LOG_SAVE:
			save_live_log();
			break;

		case ID_LOG_FILE_VIEW_FILE:
			ShellExecuteA(hwnd, "open", "notepad.exe", _log_filename, NULL, SW_SHOWNORMAL);
			break;

		case ID_LOG_FILE_VIEW_DIRECTORY:
			ShellExecuteA(hwnd, "explore", _program_data_directory, NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_CONFIG_FILE_EDIT_FILE:
			ShellExecuteA(hwnd, "runas", "notepad.exe", _config_filename, NULL, SW_SHOWNORMAL);
			break;

		case ID_CONFIG_FILE_VIEW_FILE:
			ShellExecuteA(hwnd, "open", "notepad.exe", _config_filename, NULL, SW_SHOWNORMAL);
			break;

		case ID_CONFIG_FILE_VIEW_DIRECTORY:
			ShellExecuteA(hwnd, "explore", _program_data_directory, NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_CONFIG_FILE_VIEW_DEFAULT:
			ShellExecuteA(hwnd, "open", "notepad.exe", _config_default_filename, NULL, SW_SHOWNORMAL);
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
	char *p;
	HRESULT hrc;
	int rc;
	WNDCLASSEX wc;
	MSG msg;
	const char *class_name = "brickd_logviewer";

	(void)hPrevInstance;

	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	if (strcmp(lpCmdLine, "brickd-restart") == 0) {
		rc = (int)ShellExecuteA(NULL, "runas", "cmd.exe", "/c sc stop \"Brick Daemon\"", NULL, SW_HIDE);

		if (rc <= 32) {
			report_error("Could not (re)start Brick Daemon: %s (%d)", get_error_name(rc), rc);

			return 0;
		}

		rc = (int)ShellExecuteA(NULL, "runas", "cmd.exe", "/c sc start \"Brick Daemon\"", NULL, SW_HIDE);

		if (rc <= 32) {
			report_error("Could not (re)start Brick Daemon: %s (%d)", get_error_name(rc), rc);
		}

		return 0;
	} else if (strcmp(lpCmdLine, "brickd-stop") == 0) {
		rc = (int)ShellExecuteA(NULL, "runas", "cmd.exe", "/c sc stop \"Brick Daemon\"", NULL, SW_HIDE);

		if (rc <= 32) {
			report_error("Could not stop Brick Daemon: %s (%d)", get_error_name(rc), rc);
		}

		return 0;
	}

	ptr_GetSystemTimePreciseAsFileTime =
	  (GETSYSTEMTIMEPRECISEASFILETIME)GetProcAddress(GetModuleHandleA("kernel32"),
	                                                 "GetSystemTimePreciseAsFileTime");

	_hinstance = hInstance;

	if (GetModuleFileNameA(NULL, _executable_filename, sizeof(_executable_filename)) == 0)  {
		rc = GetLastError();

		report_error("Could not get executable filename: %s (%d)", get_error_name(rc), rc);

		return 0;
	}

	string_copy(_executable_directory, sizeof(_executable_directory), _executable_filename, -1);

	p = strrchr(_executable_directory, '\\');

	if (p == NULL) {
		report_error("Executable filename is malformed: %s\n", _executable_filename);

		return 0;
	}

	*p = '\0';

	hrc = SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, _program_data_directory);

	if (!SUCCEEDED(hrc)) {
		report_error("Could not get program data directory: %08x\n", hrc);

		return 0;
	}

	string_append(_program_data_directory, sizeof(_program_data_directory), "\\Tinkerforge\\Brickd");

	string_copy(_log_filename, sizeof(_log_filename), _program_data_directory, -1);
	string_append(_log_filename, sizeof(_log_filename), "\\brickd.log");

	string_copy(_config_filename, sizeof(_config_filename), _program_data_directory, -1);
	string_append(_config_filename, sizeof(_config_filename), "\\brickd.ini");

	string_copy(_config_default_filename, sizeof(_config_default_filename), _executable_directory, -1);
	string_append(_config_default_filename, sizeof(_config_default_filename), "\\brickd-default.ini");

	_live_log_queue_items[0] = calloc(MAX_LIVE_LOG_QUEUE_LENGTH, sizeof(LiveLogItem));
	_live_log_queue_items[1] = calloc(MAX_LIVE_LOG_QUEUE_LENGTH, sizeof(LiveLogItem));

	InitializeCriticalSection(&_live_log_dropped_lock);
	InitializeCriticalSection(&_live_log_queue_lock);

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = NULL;

    if (!RegisterClassEx(&wc)) {
		rc = GetLastError();

		report_error("Could not register window class: %s (%d)",
		             get_error_name(rc), rc);

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

		return 0;
	}

	SendMessage(_hwnd, WM_SETICON, ICON_BIG,
	            (LPARAM)LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON)));

	create_menu();

	if (init_common_controls() < 0 ||
	    create_status_bar() < 0||
	    create_live_log_view() < 0 ) {
		return 0;
	}

	ShowWindow(_live_log_view, SW_SHOW);
	SetFocus(_live_log_view);
	UpdateWindow(_live_log_view);

	set_live_log_level(LOG_LEVEL_INFO);

	ShowWindow(_hwnd, nCmdShow);
	UpdateWindow(_hwnd);

	if (CreateThread(NULL, 0, read_live_log, NULL, 0, NULL) == NULL) {
		rc = GetLastError();

		report_error("Could not create live log read thread: %s (%d)",
		             get_error_name(rc), rc);
	}

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

	return msg.wParam;
}
