@setlocal

@if "%1" == "oacr" (
 move Makefile Makefile.disabled
 echo WDK build with OACR
 build -bcwgZ
 move Makefile.disabled Makefile
 goto done
)

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /DWIN32_LEAN_AND_MEAN /DNDEBUG
@set LD=link /nologo /subsystem:windows /debug /opt:ref /opt:icf
@set MC=mc
@set RC=rc /dWIN32 /r

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

@set CC=%CC% /I..

@del *.obj *.res *.bin *.exp *.pdb *.exe log_messages.h log_messages.rc

%MC% -A -b ../../../brickd/log_messages.mc

%CC% logviewer.c

%RC% /folog_messages.res log_messages.rc
%RC% /fologviewer.res logviewer.rc

%LD% /out:logviewer.exe *.obj *.res advapi32.lib comctl32.lib comdlg32.lib user32.lib

@del *.obj *.res *.bin *.exp log_messages.h log_messages.rc

:done
@endlocal
