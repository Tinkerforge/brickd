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
 set CC=%CC% /I%CRT_INC_PATH%
 set LD=%LD% /libpath:%SDK_LIB_PATH:~0,-2%\i386^
  /libpath:%CRT_LIB_PATH:~0,-2%\i386 %SDK_LIB_PATH:~0,-2%\i386\msvcrt_*.obj
 set RC=%RC% /i%CRT_INC_PATH%
 echo WDK build
) else (
 set CC=%CC% /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE
 echo non-WDK build
)

@set CC=%CC% /I..

@del *.obj *.res *.bin *.exp *.pdb *.exe

%CC% logviewer.c

%RC% /fologviewer.res logviewer.rc

%LD% /out:logviewer.exe *.obj *.res advapi32.lib comctl32.lib comdlg32.lib user32.lib shell32.lib

@del *.obj *.res *.bin *.exp

:done
@endlocal
