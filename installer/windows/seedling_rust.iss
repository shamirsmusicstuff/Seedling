; Inno Setup script for the Rust build.
; Build:  iscc /DSeedVer=0.6.0 /DVst3=<path to Seedling.vst3> /DClap=<path to Seedling.clap> seedling_rust.iss
#ifndef SeedVer
  #define SeedVer "0.0.0"
#endif
#ifndef Vst3
  #define Vst3 "..\..\rust\target\bundled\Seedling.vst3"
#endif
#ifndef Clap
  #define Clap "..\..\rust\target\bundled\Seedling.clap"
#endif

[Setup]
AppId={{6B0F8C0E-5EED-4A11-9D3A-5EED11A60002}
AppName=Seedling (Rust)
AppVersion={#SeedVer}
AppPublisher=Seedling Audio
DefaultDirName={commoncf64}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=Seedling-Rust-{#SeedVer}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Seedling (VST3 + CLAP)

[Components]
Name: "vst3"; Description: "VST3 (Ableton Live, Reaper, Bitwig, Cubase ...)"; Types: full custom; Flags: fixed
Name: "clap"; Description: "CLAP (Bitwig, Reaper ...)"; Types: full

[Files]
; nih-plug writes a single .vst3 / .clap file on Windows; a folder bundle is handled too (decided when the script is compiled).
#if DirExists(Vst3)
Source: "{#Vst3}\*"; DestDir: "{commoncf64}\VST3\Seedling.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#else
Source: "{#Vst3}"; DestDir: "{commoncf64}\VST3"; Components: vst3; Flags: ignoreversion
#endif
#if DirExists(Clap)
Source: "{#Clap}\*"; DestDir: "{commoncf64}\CLAP\Seedling.clap"; Components: clap; Flags: ignoreversion recursesubdirs createallsubdirs
#else
Source: "{#Clap}"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
#endif

[Messages]
FinishedLabel=Seedling is installed in the common VST3 / CLAP folders. Rescan plug-ins in your host to find it.%nPresets, wavetables and skins live in Documents\Seedling.
