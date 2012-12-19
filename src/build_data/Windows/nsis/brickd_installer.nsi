Name "Brick Daemon <<BRICKD_DOT_VERSION>>"

OutFile "brickd_windows_<<BRICKD_UNDERSCORE_VERSION>>.exe"

XPStyle on

; The default installation directory
InstallDir $PROGRAMFILES\Tinkerforge\Brickd

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\Tinkerforge\Brickd" "Install_Dir"

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------

!define BRICKD_VERSION <<BRICKD_DOT_VERSION>>

;--------------------------------

!macro macrouninstall

  ; Remove service
  IfFileExists "$INSTDIR\brickd_windows.exe" v1 v2
  v1:
  ExecWait '"$INSTDIR\brickd_windows.exe" stop' $0
  ExecWait '"$INSTDIR\brickd_windows.exe" stop' $1
  ExecWait '"$INSTDIR\brickd_windows.exe" remove' $2
  DetailPrint "$0 $1 $2"
  Goto remove_done
  v2:
  ExecWait '"$INSTDIR\brickd.exe" --uninstall' $0
  DetailPrint "$0"
  remove_done:

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge" ; Remove old key too
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd"
  DeleteRegKey HKLM "Software\Tinkerforge\Brickd"

  Delete $INSTDIR\drivers\*

  ; Remove files and uninstaller
  Delete $INSTDIR\*

  ; Remove directories used
  RMDir /R "$INSTDIR"

!macroend

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

!include "WordFunc.nsh"
!insertmacro VersionCompare

; The stuff to install
Section "Install Brick Daemon ${BRICKD_VERSION} Program"

  ; Check to see if already installed
  ClearErrors
  ReadRegStr $0 HKLM "Software\Tinkerforge\Brickd" "Version"
  IfErrors install ; Version not set, install
  ${VersionCompare} $0 ${BRICKD_VERSION} $1
  IntCmp $1 2 uninstall
    MessageBox MB_YESNO|MB_ICONQUESTION "Brick Daemon version $0 seems to be already installed on your system.$\n\
    Would you like to proceed with the installation of version ${BRICKD_VERSION}? $\n\
    Old version will be uninstalled first." \
        /SD IDYES IDYES uninstall IDNO quit

  quit:
    Quit

  uninstall:
    !insertmacro macrouninstall

  install:

  SetOutPath $INSTDIR
  File "..\*"

  SetOutPath $INSTDIR\drivers
  File /r "..\drivers\*"

  ; Write the installation path into the registry
  WriteRegStr HKLM "Software\Tinkerforge\Brickd" "Install_Dir" "$INSTDIR"
  WriteRegStr HKLM "Software\Tinkerforge\Brickd" "Version" ${BRICKD_VERSION}

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "DisplayName" "Tinkerforge Brick Daemon ${BRICKD_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "DisplayVersion" "${BRICKD_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "Publisher" "Tinkerforge GmbH"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

SectionEnd

Section "Register Brick Daemon ${BRICKD_VERSION} Service"

  ; Install service
  ExecWait '"$INSTDIR\brickd.exe" --install' $0
  DetailPrint "$0"

  MessageBox MB_OK "A driver installation might be necessary when devices will be connected.$\n\
    Please choose manually the driver folder in your brickd installation to install the drivers." /SD IDOK

  MessageBox MB_YESNO "A reboot is required to finish the installation. Do you wish to reboot now?" /SD IDNO IDNO quit
    Reboot
  quit:

SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"

  !insertmacro macrouninstall

SectionEnd
