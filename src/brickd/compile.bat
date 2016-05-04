@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 rmdir /s /q ..\oacr_tmp
 mkdir ..\oacr_tmp
 xcopy sources ..\oacr_tmp
 xcopy *.c ..\oacr_tmp
 xcopy *.h ..\oacr_tmp
 xcopy ..\daemonlib\*.c ..\oacr_tmp
 cd ..\oacr_tmp
 build -bcwgZ
 cd ..\brickd
 move Makefile.disabled Makefile
 goto done
)

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214^
 /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /DWIN32_LEAN_AND_MEAN /DNDEBUG^
 /DDAEMONLIB_WITH_LOGGING
@set MC=mc
@set RC=rc /dWIN32 /r
@set LD=link /nologo /opt:ref /opt:icf /release
@set AR=link /lib /nologo
@set MT=mt /nologo

@if defined DDKBUILDENV (
 set CC=%CC% /I%CRT_INC_PATH% /DBRICKD_WDK_BUILD
 set LD=%LD% /libpath:%SDK_LIB_PATH:~0,-2%\i386^
  /libpath:%CRT_LIB_PATH:~0,-2%\i386 %SDK_LIB_PATH:~0,-2%\i386\msvcrt_*.obj
 set RC=%RC% /i%CRT_INC_PATH%
 echo WDK build
) else (
 set CC=%CC% /D_CRT_SECURE_NO_WARNINGS
 echo non-WDK build
)

@set CC=%CC% /I..\build_data\windows /I..\build_data\windows\libusb /I..
@set LD=%LD% /libpath:..\build_data\windows\libusb

@del *.obj *.res *.bin *.exp *.manifest *.pdb *.exe

%MC% -A -b log_messages.mc

%CC% /FI..\brickd\fixes_msvc.h^
 ..\daemonlib\array.c^
 ..\daemonlib\base58.c^
 ..\daemonlib\config.c^
 ..\daemonlib\conf_file.c^
 ..\daemonlib\enum.c^
 ..\daemonlib\event.c^
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
 main_windows.c^
 network.c^
 service.c^
 sha1.c^
 stack.c^
 usb.c^
 usb_stack.c^
 usb_transfer.c^
 usb_winapi.c^
 websocket.c^
 zombie.c

%RC% /folog_messages.res log_messages.rc
%RC% /fobrickd.res brickd.rc

%LD% /out:brickd.exe *.obj *.res libusb-1.0.lib advapi32.lib user32.lib ws2_32.lib

@if exist brickd.exe.manifest^
 %MT% /manifest brickd.exe.manifest -outputresource:brickd.exe

@del *.obj *.res *.bin *.exp *.manifest

@if not exist ..\dist mkdir ..\dist
copy brickd.exe ..\dist\
copy ..\build_data\windows\libusb\libusb-1.0.dll ..\dist\

:done
@endlocal
