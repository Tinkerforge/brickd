; BOOL UpdateDriverForPlugAndPlayDevices(HWND, LPCTSTR, LPCTSTR, DWORD, PBOOL)
!define sysUpdateDriverForPlugAndPlayDevices "newdev::UpdateDriverForPlugAndPlayDevices(i, t, t, i, *i) i"
!define NO_ERROR              0
!define ERROR_FILE_NOT_FOUND  2
!define ERROR_IN_WOW64        -536870347  ; 0xE0000235
!define ERROR_INVALID_FLAGS   1004        ; 0x3EC
!define ERROR_NO_SUCH_DEVINST -536870389  ; 0xE000020B
!define ERROR_NO_MORE_ITEMS   259
; the masked value of ERROR_NO_SUCH_DEVINST is 523

!define INSTALLFLAG_FORCE           0x1
!define INSTALLFLAG_READONLY        0x2
!define INSTALLFLAG_NONINTERACTIVE  0x4
; Use FORCE flag ex: pushing an older release over top of new one.

; BOOL SetupCopyOEMInf(PSTR, PSTR, DWORD, DWORD, PSTR, DWORD, PDWORD, PSTR)
!define sysSetupCopyOEMInf "setupapi::SetupCopyOEMInf(t, t, i, i, i, i, *i, t) i"
!define SPOST_NONE 0
!define SPOST_PATH 1
!define SPOST_URL 2
!define SP_COPY_DELETESOURCE 0x1
!define SP_COPY_REPLACEONLY 0x2
!define SP_COPY_NOOVERWRITE 0x8
!define SP_COPY_OEMINF_CATALOG_ONLY 0x40000




; Written by Kuba Ober
; Copyright (c) 2004 Kuba Ober
;
; Permission is hereby granted, free of charge, to any person obtaining a
; copy of this software and associated documentation files (the "Software"),
; to deal in the Software without restriction, including without limitation
; the rights to use, copy, modify, merge, publish, distribute, sublicense,
; and/or sell copies of the Software, and to permit persons to whom the
; Software is furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
; DEALINGS IN THE SOFTWARE.

;
; U S A G E
;
; Push "c:\program files\yoursoftware\driver"
;  -- the directory of the .inf file
; Push "c:\program files\yoursoftware\driver\driver.inf"
;  -- the filepath of the .inf file (directory + filename)
; Push "USB\VID_1234&PID_5678"
;  -- the HID (Hardware ID) of your device
; Call InstallUpgradeDriver
;
; Your driver (minimally the .inf and .sys files) should already by installed
; by your NSIS script.
;
; Typically, you would put the driver either in $INSTDIR or $INSTDIR\Driver
; It's up to you, of course.
;
; The driver (i.e. .inf, .sys and related files) must be present for the
; lifetime of your application, you shouldn't remove them after calling
; this function!
;
; You DON'T want to put the driver in any of system directories. Windows
; will do it when the device is first plugged in.

Function InstallUpgradeDriver

  Pop $R0 ; HID
  Pop $R1 ; INFPATH
  Pop $R2 ; INFDIR
  Pop $R3 ; name

;lbl_upgrade:
  System::Get '${sysUpdateDriverForPlugAndPlayDevices}'
  Pop $0
  StrCmp $0 'error' lbl_noapi
  DetailPrint "Updating $R3 driver..."
  ; 0, HID, INFPATH, 0, 0
  Push $INSTDIR ; Otherwise this function will swallow it, dunno why
  System::Call '${sysUpdateDriverForPlugAndPlayDevices}?e (0, R0, R1, 1, 0) .r0'
  Pop $1 ; last error
  Pop $INSTDIR
  IntCmp $0 1 lbl_done
  IntCmp $1 ${ERROR_IN_WOW64} lbl_wow64
  IntCmp $1 ${ERROR_NO_SUCH_DEVINST} lbl_notplugged
  DetailPrint "$R3 driver update has failed. ($0,$1)"
  Goto lbl_noupgrade

lbl_wow64:
  nsExec::ExecToStack '"$INSTDIR\drivers\drvupd64.exe" "$R0" "$R1"'
  Pop $0 # return value/error/timeout
  Pop $1 # printed text, up to ${NSIS_MAX_STRLEN}
  IntCmp $0 0 lbl_done
  IntCmp $0 ${ERROR_NO_SUCH_DEVINST} lbl_notplugged
  DetailPrint "$R3 driver update has failed. (0,$0)"
  Goto lbl_noupgrade

lbl_notplugged:
  DetailPrint "Device is not plugged in, driver update skipped."
  Goto lbl_noupgrade

lbl_noapi:
  DetailPrint "Your Windows version doesn't support driver updates."

lbl_noupgrade:
  ; Pre-install the driver
  System::Get '${sysSetupCopyOEMInf}'
  Pop $0
  StrCmp $0 'error' lbl_inoapi
  DetailPrint "Installing $R3 driver..."
  ; INFPATH, INFDIR, SPOST_PATH, "", 0, 0, 0, 0
  System::Call '${sysSetupCopyOEMInf}?e (R1, R2, ${SPOST_PATH}, 0, 0, 0, 0, 0) .r0'
  Pop $1 ; last error
  IntCmp $0 1 lbl_nodriver
  DetailPrint "$R3 driver pre-installation has failed. ($0,$1)"
  Goto lbl_done

lbl_inoapi:
  DetailPrint "Your Windows version doesn't support driver pre-installation."

lbl_nodriver:
lbl_done:

FunctionEnd




Name "Brick Daemon <<BRICKD_DOT_VERSION>>"

OutFile "brickd_windows_<<BRICKD_UNDERSCORE_VERSION>>.exe"

XPStyle on

; The default installation directory
InstallDir "$PROGRAMFILES\Tinkerforge\Brickd"

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
  DetailPrint "Unregister service..."
  IfFileExists "$INSTDIR\brickd_windows.exe" v1 v2

v1:
  nsExec::Exec '"$INSTDIR\brickd_windows.exe" stop'
  nsExec::Exec '"$INSTDIR\brickd_windows.exe" stop'
  nsExec::Exec '"$INSTDIR\brickd_windows.exe" remove'
  Goto unregister_done

v2:
  nsExec::Exec '"$INSTDIR\brickd.exe" --uninstall'

unregister_done:
  DetailPrint "Uninstall Brick Daemon..."

  Delete "$INSTDIR\drivers\*"

  ; Remove files and uninstaller
  Delete "$INSTDIR\*"

  ; Remove directories used
  RMDir /R "$INSTDIR"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge" ; Remove old key too
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tinkerforge Brickd"
  DeleteRegKey HKLM "Software\Tinkerforge\Brickd"

!macroend

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

Section /o "-uninstall old brickd" SEC_UNINSTALL_OLD

  !insertmacro macrouninstall

SectionEnd

;--------------------------------

; The stuff to install
Section "Install Brick Daemon ${BRICKD_VERSION}"
  SectionIn RO

  DetailPrint "Install Brick Daemon..."

  SetOutPath "$INSTDIR"
  File "..\*"

  SetOutPath "$INSTDIR\drivers"
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

;--------------------------------

Section "Register Brick Daemon ${BRICKD_VERSION} as Service"

  DetailPrint "Register as service..."
  nsExec::ExecToStack '"$INSTDIR\brickd.exe" --install'
  Pop $0 # return value/error/timeout
  Pop $1 # printed text, up to ${NSIS_MAX_STRLEN}
  IntCmp $0 0 done
  DetailPrint "Service registration failed with exit code $0"
  MessageBox MB_OK|MB_ICONEXCLAMATION "Service registration failed with exit code $0:$\n$\n$1"
done:

SectionEnd

;--------------------------------

Section /o "-install driver" SEC_INSTALL_DRIVER

  Push "Brick"
  Push "$INSTDIR\drivers\brick"
  Push "$INSTDIR\drivers\brick\brick.inf"
  Push "USB\VID_16D0&PID_063D"
  Call InstallUpgradeDriver

SectionEnd

;--------------------------------

!include "Sections.nsh"
!include "WinVer.nsh"

Function .onInit

  ; Check to see if already installed
  ClearErrors
  ReadRegStr $0 HKLM "Software\Tinkerforge\Brickd" "Version"
  IfErrors not_installed ; Version not set

  SectionSetText ${SEC_UNINSTALL_OLD} "Uninstall Brick Daemon $0" ; make item visible
  IntOp $0 ${SF_SELECTED} | ${SF_RO}
  SectionSetFlags ${SEC_UNINSTALL_OLD} $0

not_installed:

 ; install driver only on systems < Windows 8
${If} ${AtMostWin2008R2}
  SectionSetText ${SEC_INSTALL_DRIVER} "Install/Update Brick Driver" ; make item visible
  SectionSetFlags ${SEC_INSTALL_DRIVER} ${SF_SELECTED}
${EndIf}

FunctionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"

  !insertmacro macrouninstall

SectionEnd
