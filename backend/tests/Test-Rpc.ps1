param(
    [Parameter(Mandatory)][string]$ServerPath,
    [Parameter(Mandatory)][string]$ClientPath
)

$ErrorActionPreference = 'Stop'
$serverExe = (Resolve-Path -LiteralPath $ServerPath).Path
$clientExe = (Resolve-Path -LiteralPath $ClientPath).Path
$dictionary = (Resolve-Path -LiteralPath "$PSScriptRoot/../../Jyutping/Resources/ime.sqlite3").Path
$fixture = [IO.Directory]::CreateTempSubdirectory('jyutping-rpc-')
$memory = Join-Path $fixture.FullName 'memory.sqlite3'
$settingsKey = "Software\JyutpingRpcSmoke-$([Guid]::NewGuid().ToString('N'))"
$serverArguments = @($dictionary, '--memory', $memory, '--settings-key', $settingsKey)

function Start-TestProcess([string]$executable, [string[]]$arguments = @()) {
    $info = [Diagnostics.ProcessStartInfo]::new($executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    [Diagnostics.Process]::Start($info)
}

function Read-TestLine($process, [string]$name) {
    $line = $process.StandardOutput.ReadLineAsync()
    if (!$line.Wait(5000)) { throw "$name timed out" }
    if (!$line.Result) {
        $errorText = if ($process.HasExited) { $process.StandardError.ReadToEnd() }
        throw "$name exited without a result. $errorText"
    }
    $line.Result
}

function Stop-TestProcess($process) {
    if ($process) {
        if (!$process.HasExited) { $process.Kill() }
        if (!$process.WaitForExit(5000)) { throw "Could not stop process $($process.Id)" }
        $process.Dispose()
    }
}

function Send-Command($process, [string]$command) {
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

function Assert-Passed($process, [string]$name) {
    $result = Read-TestLine $process $name
    if ($result -notmatch '^PASS \(') { throw "$name returned $result" }
    Write-Output "$name passed"
}

$first = $null
$second = $null
$server = $null
$duplicate = $null
try {
    $server = Start-TestProcess $serverExe $serverArguments
    if ((Read-TestLine $server 'Listener') -ne 'Listening on local RPC') { throw 'Expected a new listener for this logon' }
    $first = Start-TestProcess $clientExe
    $second = Start-TestProcess $clientExe
    Send-Command $first 'query'
    Send-Command $second 'empty'
    Assert-Passed $first 'Candidate'
    Assert-Passed $second 'Concurrent empty reply'
    Send-Command $first 'protocol'
    Assert-Passed $first 'Protocol negotiation, identity, normal and reverse candidate lists'
    Send-Command $first 'learn'
    Assert-Passed $first 'Remember and rank a learned candidate'
    Send-Command $first 'settings'
    Assert-Passed $first 'Validated field update and unchanged revision'
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($settingsKey, $true)
    try {
        $key.SetValue('CommentFontSize', 19, [Microsoft.Win32.RegistryValueKind]::DWord)
    }
    finally {
        $key.Dispose()
    }
    Send-Command $second 'verify-settings'
    Assert-Passed $second 'External field change visible to another client'

    $duplicate = Start-TestProcess $serverExe $serverArguments
    if ((Read-TestLine $duplicate 'Duplicate listener') -notmatch 'already running' -or !$duplicate.WaitForExit(5000) -or $duplicate.ExitCode -ne 0) {
        throw 'Duplicate listener did not exit successfully'
    }

    Stop-TestProcess $server
    $server = $null
    Send-Command $first 'disconnected'
    Assert-Passed $first 'Disconnect'
    $server = Start-TestProcess $serverExe $serverArguments
    if ((Read-TestLine $server 'Restarted listener') -ne 'Listening on local RPC') { throw 'Listener restart failed' }
    Send-Command $second 'memory'
    Assert-Passed $second 'Learned candidate survives restart'
    Send-Command $second 'forget'
    Assert-Passed $second 'Forget and refresh suggestions'
    Send-Command $second 'learn'
    Assert-Passed $second 'Learn before clearing'
    Send-Command $second 'clear'
    Assert-Passed $second 'Clear rejects an old learning generation'
    Send-Command $second 'verify-settings'
    Assert-Passed $second 'Settings survive restart'
    Send-Command $first 'query'
    Assert-Passed $first 'Reconnect from the same client'
}
finally {
    Stop-TestProcess $duplicate
    Stop-TestProcess $first
    Stop-TestProcess $second
    Stop-TestProcess $server
    foreach ($file in @($memory, "$memory-journal")) {
        if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    }
    $fixture.Delete()
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($settingsKey, $false)
}
