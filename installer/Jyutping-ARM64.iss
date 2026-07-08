#ifndef StageRoot
#define StageRoot "stage\ARM64"
#endif

#include "Jyutping.common.iss"

[Setup]
OutputBaseFilename=jyutping-v{#AppVersion}-arm64
ArchitecturesAllowed=arm64 and x64compatible
ArchitecturesInstallIn64BitMode=arm64

[Files]
Source: "{#StageRoot}\Jyutping.dll"; DestDir: "{autopf64}\Jyutping"; DestName: "Jyutping.dll"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\uninstall.exe"; DestDir: "{autopf64}\Jyutping"; DestName: "uninstall.exe"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\ime.sqlite3"; DestDir: "{autopf64}\Jyutping"; DestName: "ime.sqlite3"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{autopf64}\Jyutping"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
    if CurStep = ssInstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), False);
    end else if CurStep = ssPostInstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), True);
    end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
    if CurUninstallStep = usUninstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), False);
    end;
end;
