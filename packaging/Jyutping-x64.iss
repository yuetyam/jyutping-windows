#ifndef StageRoot
#define StageRoot "stage\x64"
#endif

#include "Jyutping.common.iss"

[Setup]
OutputBaseFilename=jyutping-v{#AppVersion}-x64
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible and not arm64

[Files]
Source: "{#StageRoot}\x64\Jyutping.dll"; DestDir: "{autopf64}\Jyutping"; DestName: "Jyutping.dll"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\ime.sqlite3"; DestDir: "{autopf64}\Jyutping"; DestName: "ime.sqlite3"; Flags: ignoreversion
Source: "{#StageRoot}\x86\Jyutping.dll"; DestDir: "{autopf32}\Jyutping"; DestName: "Jyutping.dll"; Flags: ignoreversion restartreplace
Source: "{#StageRoot}\ime.sqlite3"; DestDir: "{autopf32}\Jyutping"; DestName: "ime.sqlite3"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{autopf32}\Jyutping"
Type: dirifempty; Name: "{autopf64}\Jyutping"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
    if CurStep = ssInstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), False);
        RequireRegSvr32(ExpandConstant('{syswow64}\regsvr32.exe'), ExpandConstant('{autopf32}\Jyutping\Jyutping.dll'), False);
    end else if CurStep = ssPostInstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), True);
        RequireRegSvr32(ExpandConstant('{syswow64}\regsvr32.exe'), ExpandConstant('{autopf32}\Jyutping\Jyutping.dll'), True);
    end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
    if CurUninstallStep = usUninstall then begin
        RequireRegSvr32(ExpandConstant('{sys}\regsvr32.exe'), ExpandConstant('{autopf64}\Jyutping\Jyutping.dll'), False);
        RequireRegSvr32(ExpandConstant('{syswow64}\regsvr32.exe'), ExpandConstant('{autopf32}\Jyutping\Jyutping.dll'), False);
    end;
end;
