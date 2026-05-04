[Setup]
AppId={{D1A0A4A6-5E8C-4B5D-8F0E-6A2E84A4F7D4}
AppName=Rumble
AppVersion=1.0.0-beta
AppPublisher=Mark Chandler / Rumble
PrivilegesRequired=admin
; 64-bit hosts must install VST3 under native Common Files (not WOW64 Program Files (x86)).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64
OutputDir=Output
OutputBaseFilename=Rumble_Setup
DefaultDirName={autopf}\Rumble
DefaultGroupName=Rumble
DisableDirPage=yes
DisableProgramGroupPage=yes
Compression=zip
SolidCompression=no
VersionInfoCompany=Mark Chandler / Rumble
VersionInfoDescription=Rumble Synthesizer Installer
VersionInfoVersion=1.0.0.0
WizardStyle=modern

; Optional: uncomment and point to your icon if available.
; SetupIconFile=assets\rumble.ico

[Tasks]
Name: "desktopmanual"; Description: "Create a Desktop Shortcut for the README manual"; GroupDescription: "Additional Icons:"

[Files]
; VST3 bundle (recursive copy)
Source: "installer_dist\Rumble.vst3\*"; DestDir: "{commoncf}\VST3\Rumble.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; Presets
Source: "installer_dist\Presets\*"; DestDir: "{userdocs}\Rumble\Presets"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Optional PDF manual (if present in the script directory)
Source: "installer_dist\README.pdf"; DestDir: "{userdocs}\Rumble"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; Optional desktop shortcut to the manual PDF
Name: "{autodesktop}\Rumble README"; Filename: "{userdocs}\Rumble\README.pdf"; Tasks: desktopmanual
