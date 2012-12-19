Name "Brickv <<BRICKV_DOT_VERSION>>"

OutFile "brickv_windows_<<BRICKV_UNDERSCORE_VERSION>>.exe"

XPStyle on

; The default installation directory
InstallDir $PROGRAMFILES\Tinkerforge\Brickv

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\Tinkerforge\Brickv" "Install_Dir"

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------

!define BRICKV_VERSION <<BRICKV_DOT_VERSION>>

;--------------------------------

!macro macrouninstall

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Brickv" ; Remove old key too
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv"
  DeleteRegKey HKLM "Software\Tinkerforge\Brickv"

  ; Remove directories used
  RMDir /R $INSTDIR\drivers
  RMDir /R $INSTDIR\imageformats
  RMDir /R $INSTDIR\OpenGL
  RMDir /R $INSTDIR\OpenGL_accelerate
  RMDir /R $INSTDIR\plugin_system
  RMDir /R $INSTDIR\tcl
  RMDir /R "$INSTDIR"

  ; Remove menu shortcuts
  RMDir /R "$SMPROGRAMS\Tinkerforge"

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
Section "Install Brickv ${BRICKV_VERSION} Program"

; Check to see if already installed
  ClearErrors
  ReadRegStr $0 HKLM "Software\Tinkerforge\Brickv" "Version"
  IfErrors install ;Version not set, install
  ${VersionCompare} $0 ${BRICKV_VERSION} $1
  IntCmp $1 2 uninstall
    MessageBox MB_YESNO|MB_ICONQUESTION "Brickv version $0 seems to be already installed on your system.$\n\
    Would you like to proceed with the installation of version ${BRICKV_VERSION}?$\n\
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

  SetOutPath $INSTDIR\imageformats
  File /r "..\imageformats\*"

  SetOutPath $INSTDIR\OpenGL
  File /r "..\OpenGL\*"

  SetOutPath $INSTDIR\plugin_system
  File /r "..\plugin_system\*"

  ; Write the installation path into the registry
  WriteRegStr HKLM "Software\Tinkerforge\Brickv" "Install_Dir" "$INSTDIR"
  WriteRegStr HKLM "Software\Tinkerforge\Brickv" "Version" ${BRICKV_VERSION}

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "DisplayName" "Tinkerforge Brickv ${BRICKV_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "DisplayIcon" '"$INSTDIR\brickv-icon.ico"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "DisplayVersion" "${BRICKV_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "Publisher" "Tinkerforge GmbH"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickv" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

  ; Create start menu shortcut
  SetOutPath $INSTDIR\ ; set working directory for main.exe
  createDirectory "$SMPROGRAMS\Tinkerforge"
  createShortCut "$SMPROGRAMS\Tinkerforge\Brickv ${BRICKV_VERSION}.lnk" "$INSTDIR\main.exe"

SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"

  !insertmacro macrouninstall

SectionEnd
