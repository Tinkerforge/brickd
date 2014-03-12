@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 build -bcwgZ
 move Makefile.disabled Makefile
 goto done
)

rem @set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /FI..\brickd\fixes_msvc.h /FIbool_msvc.h /DWIN32_LEAN_AND_MEAN /DNDEBUG
@set CC=cl /nologo /c /MD /Zi /EHsc /Oy- /Ob0 /W4 /wd4200 /wd4214 /FI..\brickd\fixes_msvc.h /FIbool_msvc.h /DWIN32_LEAN_AND_MEAN /DDEBUG
@set MC=mc
@set RC=rc /dWIN32 /r
rem @set LD=link /nologo /opt:ref /opt:icf /release
@set LD=link /nologo /debug
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

@set CC=%CC% /I. /I..\build_data\windows

@del *.obj *.res *.bin *.exp *.manifest *.exe


%CC% array_test.c^
 ..\brickd\fixes_msvc.c^
 ..\brickd\array.c

%LD% /out:array_test.exe *.obj

@if exist array_test.exe.manifest^
 %MT% /manifest array_test.exe.manifest -outputresource:array_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% queue_test.c^
 ..\brickd\fixes_msvc.c^
 ..\brickd\queue.c

%LD% /out:queue_test.exe *.obj

@if exist queue_test.exe.manifest^
 %MT% /manifest queue_test.exe.manifest -outputresource:queue_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% throughput_test.c^
 ip_connection.c^
 brick_master.c^
 ..\brickd\fixes_msvc.c^
 ..\brickd\sha1.c^
 ..\brickd\utils.c

%LD% /out:throughput_test.exe *.obj *.res advapi32.lib ws2_32.lib

@if exist throughput_test.exe.manifest^
 %MT% /manifest throughput_test.exe.manifest -outputresource:throughput_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% sha1_test.c^
 ..\brickd\fixes_msvc.c^
 ..\brickd\sha1.c

%LD% /out:sha1_test.exe *.obj ws2_32.lib

@if exist sha1_test.exe.manifest^
 %MT% /manifest sha1_test.exe.manifest -outputresource:sha1_test.exe

@del *.obj *.res *.bin *.exp *.manifest


:done
@endlocal
