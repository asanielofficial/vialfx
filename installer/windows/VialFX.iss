; Inno Setup script for the VialFX VST3 installer (built by .github/workflows/build.yml).
; Build: ISCC /DSourceFile=C:\path\to\VialFX.vst3 /DAppVersion=1.0 installer\windows\VialFX.iss
#ifndef AppVersion
  #define AppVersion "1.0"
#endif
#ifndef SourceFile
  #define SourceFile "..\..\dist\VialFX.vst3"
#endif

[Setup]
AppId={{6F0C3C7A-5B1D-4E0A-9B52-0A1B2C3D4E5F}
AppName=VialFX
AppVersion={#AppVersion}
AppPublisher=VialFX
DefaultDirName={autopf}\VialFX
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=VialFX-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=VialFX (VST3)

[Files]
Source: "{#SourceFile}"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion

[Messages]
FinishedLabel=VialFX was installed to the system VST3 folder. Rescan plugins in your DAW.
