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
rem @set CC=cl /nologo /c /MD /Zi /EHsc /Oy- /Ob0 /W4 /wd4200 /wd4214^
rem  /FI..\brickd\fixes_msvc.h /FIbool_msvc.h /DWIN32_LEAN_AND_MEAN /DDEBUG
@set MC=mc
@set RC=rc /dWIN32 /r
@set LD=link /nologo /opt:ref /opt:icf /release
rem @set LD=link /nologo /debug
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

@set CC=%CC% /I. /I..\build_data\windows /I..

@del *.obj *.res *.bin *.exp *.manifest *.pdb *.exe


%CC% array_test.c^
 ..\brickd\fixes_msvc.c^
 ..\daemonlib\array.c

%LD% /out:array_test.exe *.obj

@if exist array_test.exe.manifest^
 %MT% /manifest array_test.exe.manifest -outputresource:array_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% queue_test.c^
 ..\brickd\fixes_msvc.c^
 ..\daemonlib\queue.c

%LD% /out:queue_test.exe *.obj

@if exist queue_test.exe.manifest^
 %MT% /manifest queue_test.exe.manifest -outputresource:queue_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% throughput_test.c^
 ip_connection.c^
 brick_master.c^
 ..\brickd\fixes_msvc.c^
 ..\daemonlib\utils.c

%LD% /out:throughput_test.exe *.obj *.res ws2_32.lib

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


%CC% putenv_test.c^
 ..\brickd\fixes_msvc.c

%LD% /out:putenv_test.exe *.obj ws2_32.lib

@if exist putenv_test.exe.manifest^
 %MT% /manifest putenv_test.exe.manifest -outputresource:putenv_test.exe

@del *.obj *.res *.bin *.exp *.manifest


%CC% base58_test.c^
 ..\brickd\fixes_msvc.c^
 ..\daemonlib\utils.c

%LD% /out:base58_test.exe *.obj ws2_32.lib

@if exist base58_test.exe.manifest^
 %MT% /manifest base58_test.exe.manifest -outputresource:base58_test.exe

@del *.obj *.res *.bin *.exp *.manifest


:done
@endlocal
