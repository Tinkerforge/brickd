@setlocal

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4201 /wd4214^
 /DWINVER=0x0600 /D_WIN32_WINNT=0x0600 /DWIN32_LEAN_AND_MEAN /DNDEBUG^
 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /DDAEMONLIB_WITH_LOGGING^
 /DBRICKD_VERSION_SUFFIX="\"%1\"" /I..\build_data\windows^
 /I..\build_data\windows\libusb /I..
@set RC=rc /dWIN32 /r
@set LD=link /nologo /opt:ref /opt:icf /libpath:..\build_data\windows\libusb
@set MT=mt /nologo

@del *.obj *.res *.bin *.exp *.manifest *.pdb *.exe

%CC% /FI..\brickd\fixes_msvc.h^
 ..\daemonlib\array.c^
 ..\daemonlib\base58.c^
 ..\daemonlib\config.c^
 ..\daemonlib\conf_file.c^
 ..\daemonlib\enum.c^
 ..\daemonlib\event.c^
 ..\daemonlib\fifo.c^
 ..\daemonlib\file.c^
 ..\daemonlib\io.c^
 ..\daemonlib\log.c^
 ..\daemonlib\node.c^
 ..\daemonlib\packet.c^
 ..\daemonlib\pipe_winapi.c^
 ..\daemonlib\queue.c^
 ..\daemonlib\socket.c^
 ..\daemonlib\socket_winapi.c^
 ..\daemonlib\threads_winapi.c^
 ..\daemonlib\timer_winapi.c^
 ..\daemonlib\utils.c^
 ..\daemonlib\writer.c

%CC% /FIfixes_msvc.h^
 base64.c^
 client.c^
 config_options.c^
 event_winapi.c^
 fixes_msvc.c^
 hardware.c^
 hmac.c^
 log_winapi.c^
 mesh.c^
 mesh_packet.c^
 mesh_stack.c^
 main_winapi.c^
 network.c^
 service.c^
 sha1.c^
 stack.c^
 usb.c^
 usb_stack.c^
 usb_transfer.c^
 usb_winapi.c^
 usb_windows.c^
 websocket.c^
 zombie.c

%RC% /fobrickd.res brickd.rc

%LD% /out:brickd.exe *.obj *.res libusb-1.0-brickd.lib advapi32.lib user32.lib ws2_32.lib shell32.lib

@if exist brickd.exe.manifest^
 %MT% /manifest brickd.exe.manifest -outputresource:brickd.exe

@del *.obj *.res *.bin *.exp *.manifest

@if not exist ..\dist mkdir ..\dist
copy brickd.exe ..\dist\
copy ..\build_data\windows\libusb\libusb-1.0-brickd.dll ..\dist\

:done
@endlocal
