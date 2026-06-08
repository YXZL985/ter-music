; windows.nsi — ter-music NSIS installer script
;
; Build:  makensis windows.nsi
; Output: ter-music-<version>-setup.exe
;
; Requires NSIS 3.0+ (https://nsis.sourceforge.io)
;
; © ter-music team

!define PRODUCT_NAME "ter-music"
!define PRODUCT_VERSION "2.0.0"
!define PRODUCT_PUBLISHER "YXZL985"
!define PRODUCT_WEB_SITE "https://github.com/YXZL985/ter-music"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\ter-music.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; ── Compiler flags ──────────────────────────────────────────────
SetCompressor lzma
RequestExecutionLevel admin

; ── Modern UI ───────────────────────────────────────────────────
!include "MUI2.nsh"

; UI pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "../../LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; UI language
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ── Installer attributes ───────────────────────────────────────
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "ter-music-${PRODUCT_VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show

; ── Sections ────────────────────────────────────────────────────

Section "Main Program" SEC_MAIN
    SetOutPath "$INSTDIR"

    ; Binary and data files
    File "..\..\build\Release\ter-music.exe"
    File "..\..\data\help-quickstart-zh.txt"
    File "..\..\data\help-quickstart-en.txt"

    ; vcpkg DLLs (redistributable)
    File /r "..\..\build\Release\*.dll"

    ; Create config directory
    SetOutPath "$APPDATA\${PRODUCT_NAME}"

    ; Shortcuts
    SetOutPath "$INSTDIR"
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\ter-music.lnk" "$INSTDIR\ter-music.exe" "" "$INSTDIR\ter-music.exe" 0
    CreateShortCut "$DESKTOP\ter-music.lnk" "$INSTDIR\ter-music.exe" "" "$INSTDIR\ter-music.exe" 0

    ; Register app paths
    WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\ter-music.exe"
    WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Path" "$INSTDIR"

SectionEnd

Section "Uninstall"
    ; Remove shortcuts
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\ter-music.lnk"
    Delete "$DESKTOP\ter-music.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; Remove installed files
    Delete "$INSTDIR\ter-music.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\help-quickstart-zh.txt"
    Delete "$INSTDIR\help-quickstart-en.txt"
    RMDir "$INSTDIR"

    ; Remove registry keys
    DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
    SetAutoClose true
SectionEnd

; ── Version info ───────────────────────────────────────────────
VIProductVersion "2.0.0.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "Terminal UI music player"
