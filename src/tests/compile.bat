@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 build -bcwgZ
 move Makefile.disabled Makefile
 goto done
)

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /FI..\brickd\fixes_msvc.h^
 /FIbool_msvc.h /DWIN32_LEAN_AND_MEAN /DNDEBUG
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

@set CC=%CC% /I. /I..\brickd /I..\build_data\windows

@del *.obj *.res *.bin *.exp *.manifest *.exe

%CC% throughput.c^
 ip_connection.c^
 brick_master.c^
 ..\brickd\fixes_msvc.c^
 ..\brickd\utils.c

%LD% /out:throughput.exe *.obj *.res ws2_32.lib

@if exist throughput.exe.manifest^
 %MT% /manifest throughput.exe.manifest -outputresource:throughput.exe

@del *.obj *.res *.bin *.exp *.manifest

:done
@endlocal
