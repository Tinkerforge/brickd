@setlocal

@set CC=cl /nologo /c /MD /O2 /W4 /wd4200 /wd4214 /DWIN32_LEAN_AND_MEAN /DNDEBUG^
 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /DBRICKD_VERSION_SUFFIX="\"%1\""
@set RC=rc /dWIN32 /r
@set LD=link /nologo /subsystem:windows /opt:ref /opt:icf

@del *.obj *.res *.bin *.exp *.pdb *.exe

%CC% main.c

%RC% /fologviewer.res logviewer.rc

%LD% /out:logviewer.exe *.obj *.res advapi32.lib comctl32.lib comdlg32.lib user32.lib shell32.lib ole32.lib

@del *.obj *.res *.bin *.exp

:done
@endlocal
