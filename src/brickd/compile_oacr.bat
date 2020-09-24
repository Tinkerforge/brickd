@setlocal

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

@endlocal
