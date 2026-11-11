; Inno Setup script for the VialFX installer: VST3 and CLAP are separate components, each can be switched off
; (built by .github/workflows/build.yml).
; Build: ISCC /DSourceFile=C:\path\to\VialFX.vst3 /DClapFile=C:\path\to\VialFX.clap /DAppVersion=1.3 installer\windows\VialFX.iss
#ifndef AppVersion
  #define AppVersion "1.3"
#endif
#ifndef SourceFile
  #define SourceFile "..\..\dist\VialFX.vst3"
#endif
#ifndef ClapFile
  #define ClapFile "..\..\dist-clap\VialFX.clap"
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
UninstallDisplayName=VialFX

[Types]
Name: "full"; Description: "Full installation (VST3 and CLAP)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (C:\Program Files\Common Files\CLAP)"; Types: full custom

[Files]
Source: "{#SourceFile}"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion; Components: vst3
Source: "{#ClapFile}"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap

[Messages]
FinishedLabel=VialFX was installed to the plugin folders you selected. Rescan plugins in your DAW.
