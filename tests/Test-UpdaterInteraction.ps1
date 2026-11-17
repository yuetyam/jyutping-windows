[CmdletBinding()]
param([string]$UpdaterPath = (Join-Path $PSScriptRoot '..\JyutpingUpdater\bin\ARM64\Debug\JyutpingUpdater.exe'))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$UpdaterPath = (Resolve-Path -LiteralPath $UpdaterPath).Path
if (Get-Process -Name JyutpingUpdater -ErrorAction SilentlyContinue) { throw 'Close the existing updater before running this interaction test.' }

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class UpdaterTestWindows {
    public class Window { public IntPtr Handle; public string Text; }
    private delegate bool EnumWindow(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindow callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumWindow callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr parameter, IntPtr data);
    public static Window[] Find(uint processId) {
        var result = new List<Window>();
        EnumWindows((window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == processId && IsWindowVisible(window)) {
                var text = new StringBuilder(8192);
                var part = new StringBuilder(1024);
                GetWindowText(window, part, part.Capacity); text.AppendLine(part.ToString());
                EnumChildWindows(window, (child, unused) => {
                    part.Clear(); GetWindowText(child, part, part.Capacity); text.AppendLine(part.ToString()); return true;
                }, IntPtr.Zero);
                result.Add(new Window { Handle = window, Text = text.ToString() });
            }
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
    public static void Close(uint processId) {
        foreach (var window in Find(processId)) PostMessage(window.Handle, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
'@

function Wait-ForDialog([System.Diagnostics.Process]$Process) {
    $deadline = [datetime]::UtcNow.AddSeconds(20)
    do {
        if ($Process.HasExited) { throw "Updater exited before showing a dialog ($($Process.ExitCode))." }
        $windows = @([UpdaterTestWindows]::Find($Process.Id))
        if ($windows.Count) { return $windows }
        Start-Sleep -Milliseconds 100
    } while ([datetime]::UtcNow -lt $deadline)
    throw 'Updater did not show its manual-check dialog.'
}

$processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
try {
    $invalid = Start-Process -FilePath $UpdaterPath -ArgumentList '--invalid-operation' -WindowStyle Hidden -PassThru
    $processes.Add($invalid)
    if (-not $invalid.WaitForExit(5000) -or $invalid.ExitCode -ne 87) { throw 'Unexpected arguments were not rejected.' }

    $primary = Start-Process -FilePath $UpdaterPath -ArgumentList '--check-for-updates' -WindowStyle Hidden -PassThru
    $processes.Add($primary)
    $windows = @(Wait-ForDialog $primary)
    $settingsKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Jyutping\WinSparkle')
    try {
        if (-not $settingsKey -or [string]$settingsKey.GetValue('CheckForUpdates') -ne '0') { throw 'Automatic update checking was not explicitly disabled.' }
    } finally { if ($settingsKey) { $settingsKey.Dispose() } }

    $secondary = Start-Process -FilePath $UpdaterPath -ArgumentList '--check-for-updates' -WindowStyle Hidden -PassThru
    $processes.Add($secondary)
    if (-not $secondary.WaitForExit(5000) -or $secondary.ExitCode -ne 0) { throw 'Repeated invocation did not activate the existing helper.' }
    if ($primary.HasExited) { throw 'The primary helper exited during activation.' }
    Start-Sleep -Milliseconds 500
    $windows = @([UpdaterTestWindows]::Find($primary.Id))
    if (-not $windows.Count) { throw 'The existing interaction disappeared after activation.' }
    [UpdaterTestWindows]::Close($primary.Id)
    if (-not $primary.WaitForExit(10000) -or $primary.ExitCode -ne 0) { throw 'The updater did not exit cleanly after dismissal.' }

    $next = Start-Process -FilePath $UpdaterPath -ArgumentList '--check-for-updates' -WindowStyle Hidden -PassThru
    $processes.Add($next)
    Wait-ForDialog $next | Out-Null
    [UpdaterTestWindows]::Close($next.Id)
    if (-not $next.WaitForExit(10000) -or $next.ExitCode -ne 0) { throw 'A subsequent manual check did not complete cleanly.' }
    Write-Host 'Updater interaction tests passed: argument validation, manual UI, disabled automatic checks, single instance, dismissal, and subsequent launch.'
} finally {
    foreach ($process in $processes) {
        if (-not $process.HasExited) {
            [UpdaterTestWindows]::Close($process.Id)
            if (-not $process.WaitForExit(5000)) { $process.Kill() }
        }
        $process.Dispose()
    }
}
