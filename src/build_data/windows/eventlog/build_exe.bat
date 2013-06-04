@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 build -bcwgZ
 move Makefile.disabled Makefile
 goto done
)

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /DWIN32_LEAN_AND_MEAN /DNDEBUG
@set MC=mc
@set RC=rc /dWIN32 /r
@set LD=link /nologo /subsystem:windows /opt:ref /opt:icf /release
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

@del *.obj *.res *.bin *.exp *.exe

%CC% eventlog.c

%LD% /out:eventlog.exe *.obj *.res advapi32.lib comctl32.lib comdlg32.lib user32.lib
%MT% /manifest eventlog.manifest -outputresource:eventlog.exe

@del *.obj *.res *.bin *.exp

:done
@endlocal
