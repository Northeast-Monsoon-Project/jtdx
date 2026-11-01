Unicode True
!include "MUI2.nsh"
!include "x64.nsh"
!include "WordFunc.nsh"
!ifndef FM_VERSION
!define FM_VERSION "3.6.0"
!endif
Name "JTDX-F V${FM_VERSION}"
OutFile "${FM_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\JTDX-F"
InstallDirRegKey HKCU "Software\Fluxmancer\JTDX-F" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "Fluxmancer | JTDX-F"
VIProductVersion "${FM_VERSION}.0"
VIAddVersionKey "ProductName" "JTDX-F"
VIAddVersionKey "CompanyName" "Fluxmancer"
VIAddVersionKey "FileDescription" "JTDX-F V${FM_VERSION} Windows x64 Installer"
VIAddVersionKey "FileVersion" "${FM_VERSION}"
VIAddVersionKey "LegalCopyright" "Fluxmancer 2026; upstream JTDX and WSJT-X copyrights retained"
!define MUI_ICON "${FM_ICON}"
!define MUI_UNICON "${FM_ICON}"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${FM_STAGE}\COPYING"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\jtdx.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "JTDX-F requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "DisplayVersion"
  ${If} $0 != ""
    ${VersionCompare} "$0" "${FM_VERSION}" $1
    ${If} $1 == 1
      MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "已安装较新版本 JTDX-F V$0，是否回到 V${FM_VERSION}？程序将被替换，日志和设置会保留。 / A newer version is installed. Downgrade and keep logs?" /SD IDNO IDYES downgrade
      Abort
      downgrade:
    ${EndIf}
  ${EndIf}
FunctionEnd
Section "JTDX-F" Main
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${FM_STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Fluxmancer\JTDX-F" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "DisplayName" "JTDX-F V${FM_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "DisplayVersion" "${FM_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "Publisher" "Fluxmancer"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"' 
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "DisplayIcon" "$INSTDIR\bin\jtdx.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\JTDX-F"
  CreateShortcut "$SMPROGRAMS\JTDX-F\JTDX-F.lnk" "$INSTDIR\bin\jtdx.exe"
  CreateShortcut "$SMPROGRAMS\JTDX-F\Documentation.lnk" "$INSTDIR\share\jtdx\docs"
  CreateShortcut "$SMPROGRAMS\JTDX-F\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\JTDX-F.lnk" "$INSTDIR\bin\jtdx.exe"
SectionEnd
Section "Uninstall"
  SetRegView 64
  SetShellVarContext current
  Delete "$DESKTOP\JTDX-F.lnk"
  Delete "$SMPROGRAMS\JTDX-F\JTDX-F.lnk"
  Delete "$SMPROGRAMS\JTDX-F\Documentation.lnk"
  Delete "$SMPROGRAMS\JTDX-F\Uninstall.lnk"
  RMDir "$SMPROGRAMS\JTDX-F"
  ; Remove only files supplied by this installer. Preserve user settings/logs.
  !include "${FM_UNINSTALL_FILES}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Fluxmancer\JTDX-F"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\JTDX-F"
SectionEnd
