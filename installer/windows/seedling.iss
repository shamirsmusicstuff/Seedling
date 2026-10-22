; Inno Setup script. Build:  iscc /DVer=0.2.0 /DSrc=<path to Seedling.vst3 folder> seedling.iss
#ifndef Ver
  #define Ver "0.0.0"
#endif
#ifndef Src
  #define Src "..\..\build\Seedling_artefacts\Release\VST3\Seedling.vst3"
#endif

[Setup]
AppId={{6B0F8C0E-5EED-4A11-9D3A-5EED11A60001}
AppName=Seedling
AppVersion={#Ver}
AppPublisher=Seedling Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=Seedling-{#Ver}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Seedling (VST3)

[Files]
Source: "{#Src}\*"; DestDir: "{commoncf64}\VST3\Seedling.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=Seedling (VST3) is installed in the common VST3 folder. Rescan plug-ins in your host to find it.%nPresets and skins live in Documents\Seedling.
