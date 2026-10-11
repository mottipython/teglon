; Teglon screensaver installer (per user, no administrator rights needed)
!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "1.0.4"
!endif

Name "Teglon ${VERSION}"
OutFile "..\..\dist\Teglon-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Teglon"
InstallDirRegKey HKCU "Software\Teglon" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Unicode true

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open Screen Saver Settings with Teglon selected"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenSettings

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function OpenSettings
  ; the standard way to select a screensaver: shows the settings dialog with it chosen
  GetFullPathName /SHORT $0 "$INSTDIR\Teglon.scr"
  Exec 'rundll32.exe desk.cpl,InstallScreenSaver $0'
FunctionEnd

Section "Teglon" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\..\build\win\Teglon.scr"
  File "..\..\build\win\teglon-solve.exe"
  File "..\..\build\win\*.dll"
  File /oname=README.txt "..\..\README.md"
  File /oname=LICENSE.txt "..\..\LICENSE"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\Teglon" "InstallDir" "$INSTDIR"
  ; make Teglon the current screensaver
  GetFullPathName /SHORT $0 "$INSTDIR\Teglon.scr"
  WriteRegStr HKCU "Control Panel\Desktop" "SCRNSAVE.EXE" "$0"
  WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveActive" "1"
  ReadRegStr $1 HKCU "Control Panel\Desktop" "ScreenSaveTimeOut"
  StrCmp $1 "" 0 +2
    WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveTimeOut" "300"

  CreateDirectory "$SMPROGRAMS\Teglon"
  CreateShortcut "$SMPROGRAMS\Teglon\Teglon (window).lnk" "$INSTDIR\Teglon.scr" "--window"
  CreateShortcut "$SMPROGRAMS\Teglon\Teglon screensaver.lnk" "$INSTDIR\Teglon.scr" "/s"
  CreateShortcut "$SMPROGRAMS\Teglon\Uninstall Teglon.lnk" "$INSTDIR\Uninstall.exe"

  !define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Teglon"
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "Teglon screensaver"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $2 $3 $4
  IntFmt $2 "0x%08X" $2
  WriteRegDWORD HKCU "${UNKEY}" "EstimatedSize" "$2"
SectionEnd

Section "Uninstall"
  ; stop being the screensaver, if we still are
  GetFullPathName /SHORT $0 "$INSTDIR\Teglon.scr"
  ReadRegStr $1 HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"
  StrCmp $1 $0 0 +2
    DeleteRegValue HKCU "Control Panel\Desktop" "SCRNSAVE.EXE"

  Delete "$INSTDIR\Teglon.scr"
  Delete "$INSTDIR\teglon-solve.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\SDL3.dll"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\Teglon\Teglon (window).lnk"
  Delete "$SMPROGRAMS\Teglon\Teglon screensaver.lnk"
  Delete "$SMPROGRAMS\Teglon\Uninstall Teglon.lnk"
  RMDir "$SMPROGRAMS\Teglon"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Teglon"
  DeleteRegKey HKCU "Software\Teglon"
SectionEnd
