@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 build -bcwgZ
 move Makefile.disabled Makefile
 goto done
)

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /FImsvcfixes.h^
 /DBRICKD_LOG_ENABLED /DWIN32_LEAN_AND_MEAN
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
 echo non-WDK build
)

@del *.obj *.res *.exp *.manifest *.exe

@rem @set CC=%CC% /D_CRT_SECURE_NO_WARNINGS /wd4996

@set CC=%CC% /I..\build_data\Windows\libusb
@set LD=%LD% /libpath:..\build_data\Windows\libusb

%CC%^
 brick.c^
 client.c^
 event.c^
 event_winapi.c^
 log.c^
 main_windows.c^
 msvcfixes.c^
 network.c^
 packet.c^
 pipe_winapi.c^
 socket_winapi.c^
 threads_winapi.c^
 transfer.c^
 usb.c^
 utils.c

%RC% /fobrickd.res brickd.rc

%LD% /out:brickd.exe *.obj libusb-1.0.lib brickd.res advapi32.lib user32.lib ws2_32.lib

@if exist brickd.exe.manifest^
 %MT% /manifest brickd.exe.manifest -outputresource:brickd.exe

@del *.obj *.res *.exp *.manifest

@if not exist dist mkdir dist
copy brickd.exe dist\
copy ..\build_data\Windows\libusb\libusb-1.0.dll dist\

:done
@endlocal
