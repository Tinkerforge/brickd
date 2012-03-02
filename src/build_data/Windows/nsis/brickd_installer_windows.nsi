Name "Brickd Windows Installer"

OutFile "brickd_windows.exe"

; The default installation directory
InstallDir $PROGRAMFILES\Tinkerforge\Brickd

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "SOFTWARE\TINKERFORGE\BRICKD" "Install_Dir"

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------

!define BRICKD_VERSION "1.0.1"

;--------------------------------

!macro macrouninstall

  ; Remove service
  ExecWait '"$INSTDIR\brickd_windows.exe" stop' $0
  ExecWait '"$INSTDIR\brickd_windows.exe" stop' $1
  ExecWait '"$INSTDIR\brickd_windows.exe" remove' $2
  DetailPrint "$0 $1 $2"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd"
  DeleteRegKey HKLM SOFTWARE\TINKERFORGE\BRICKD


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
Section "Install Brickd Programm"

; Check to see if already installed
  ClearErrors
  ReadRegStr $0 HKLM SOFTWARE\TINKERFORGE\BRICKD "Version"
  IfErrors install ;Version not set, install
  ${VersionCompare} $0 ${BRICKD_VERSION} $1
  IntCmp $1 2 uninstall
    MessageBox MB_YESNO|MB_ICONQUESTION "Brickd version $0 seems to be already installed on your system.$\n\
    Would you like to proceed with the installation of version ${BRICKD_VERSION}? $\n\
    Old Version will be first uninstalled." \
        IDYES uninstall IDNO quit

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
  WriteRegStr HKLM SOFTWARE\TINKERFORGE\BRICKD "Install_Dir" "$INSTDIR"
  WriteRegStr HKLM SOFTWARE\TINKERFORGE\BRICKD "Version" ${BRICKD_VERSION}
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd" "DisplayName" "Brickd"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd" "Publisher" "Tinkerforge GmbH"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge\Brickd" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

SectionEnd

Section "Install Brickd Service"

  ; Install service
  ExecWait '"$INSTDIR\brickd_windows.exe" --startup auto install' $0
  ExecWait '"$INSTDIR\brickd_windows.exe" start' $1
  DetailPrint "$0 $1"

  MessageBox MB_OK "A driver installation might be necessary when devices will be connected.$\nPlease choose manually the driver folder in your brickd installation to install the drivers."

  MessageBox MB_YESNO "A reboot is required to finish the installation. Do you wish to reboot now?" IDNO quit
    Reboot
  quit:
  
SectionEnd


;--------------------------------

; Uninstaller

Section "Uninstall"

  !insertmacro macrouninstall

SectionEnd
