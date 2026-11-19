param([Parameter(Mandatory)][string]$SettingsPath, [Parameter(Mandatory)][string]$ServerPath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$settings = (Resolve-Path -LiteralPath $SettingsPath).Path
$serverExe = (Resolve-Path -LiteralPath $ServerPath).Path
$dictionary = (Resolve-Path -LiteralPath "$PSScriptRoot/../../Jyutping/Resources/ime.sqlite3").Path
$settingsKey = "Software\JyutpingSettingsSmoke-$([Guid]::NewGuid().ToString('N'))"
$fixture = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($settingsKey)
$server = $null
$window = $null
$second = $null
$control = $null
try {
    $fixture.SetValue('CandidatePageSize', 7, [Microsoft.Win32.RegistryValueKind]::DWord)
    $info = [Diagnostics.ProcessStartInfo]::new($serverExe)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    foreach ($argument in @($dictionary, '--settings-key', $settingsKey)) { $info.ArgumentList.Add($argument) }
    $server = [Diagnostics.Process]::Start($info)
    $line = $server.StandardOutput.ReadLineAsync()
    if (!$line.Wait(5000) -or $line.Result -ne 'Listening on local RPC') { throw 'Server failed to start' }
    $appInfo = [Diagnostics.ProcessStartInfo]::new($settings)
    $appInfo.UseShellExecute = $false
    $appInfo.CreateNoWindow = $true
    $window = [Diagnostics.Process]::Start($appInfo)
    $idCondition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, 'Setting4')
    $deadline = [Environment]::TickCount64 + 15000
    do {
        if ($window.HasExited) { throw "Settings process exited $($window.ExitCode)" }
        $window.Refresh()
        $handle = $window.MainWindowHandle
        if ($handle -ne [IntPtr]::Zero) {
            $root = [Windows.Automation.AutomationElement]::FromHandle($handle)
            $control = $root.FindFirst([Windows.Automation.TreeScope]::Descendants, $idCondition)
            if ($control -and $control.Current.IsEnabled) { break }
        }
        Start-Sleep -Milliseconds 150
    } while ([Environment]::TickCount64 -lt $deadline)
    if (!$control -or !$control.Current.IsEnabled) { throw 'Candidate count setting did not load' }
    $second = [Diagnostics.Process]::Start($appInfo)
    if (!$second.WaitForExit(5000) -or $second.ExitCode) { throw 'Second launch did not redirect successfully' }
    $expand = $control.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expand.Expand()
    $selectionName = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, '8')
    $option = $control.FindFirst([Windows.Automation.TreeScope]::Descendants, $selectionName)
    if (!$option) { $option = $root.FindFirst([Windows.Automation.TreeScope]::Descendants, $selectionName) }
    if (!$option) { throw 'Candidate count choice was unavailable' }
    $option.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $deadline = [Environment]::TickCount64 + 4000
    do {
        $value = $fixture.GetValue('CandidatePageSize')
        if ($value -eq 8) { break }
        Start-Sleep -Milliseconds 100
    } while ([Environment]::TickCount64 -lt $deadline)
    if ($value -ne 8) { throw "The settings UI wrote $value instead of 8" }
    'WinUI settings loaded, redirected a second launch, and wrote the selected value through RPC.'
} finally {
    foreach ($process in @($second, $window, $server)) {
        if ($process) {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
    $fixture.Dispose()
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($settingsKey, $false)
}
