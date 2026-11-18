param([switch]$Rpc)

$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'target'
$dictionary = (Resolve-Path (Join-Path $PSScriptRoot '..\..\Jyutping\Resources\ime.sqlite3')).Path
if ($Rpc) {
    $backend = Join-Path $target 'AppContainerRpc.exe'
    $client = Join-Path $target 'rpc-x64\RpcSmoke.exe'
} else {
    $backend = Join-Path $target 'debug\jyutping-ipc-probe.exe'
    $client = Join-Path $target 'Smoke.exe'
}

function Test-Client($expected, [switch]$Invalidate) {
    $arguments = @($expected)
    if ($Invalidate) {
        $arguments += 'invalidate'
    }
    & $client @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Client failed to report '$expected' on the owner thread"
    }
}

function Test-WithBackend($delay, [scriptblock]$test) {
    $run = [guid]::NewGuid()
    $stdout = Join-Path $target "backend-$run.stdout"
    $stderr = Join-Path $target "backend-$run.stderr"
    $backendArguments = if ($Rpc) { 'serve "{0}"' -f $dictionary } else { '"{0}" {1}' -f $dictionary, $delay }
    $process = Start-Process -FilePath $backend -ArgumentList $backendArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        $deadline = [Environment]::TickCount64 + 4000
        while ((Get-Content -LiteralPath $stdout -Raw) -notmatch '^Listening ') {
            if ($process.HasExited -or [Environment]::TickCount64 -ge $deadline) {
                throw "Backend failed to start. $(Get-Content -LiteralPath $stderr -Raw)"
            }
            Start-Sleep -Milliseconds 25
        }
        & $test
    }
    finally {
        $process.Refresh()
        if ($process.HasExited) {
            Write-Output "Backend exited unexpectedly with code $($process.ExitCode)"
        }
        if (!$process.HasExited) {
            Stop-Process -InputObject $process
        }
        $process.WaitForExit()
        Get-Content -LiteralPath $stdout, $stderr
        $process.Dispose()
    }
}

if ($Rpc) {
    Test-Client 'current=1 stage=local RPC error=1722 candidate=0'
    Test-WithBackend 0 {
        Test-Client 'current=1 stage=local RPC error=0 candidate=1'
        Test-Client 'current=0 stage=local RPC error=0 candidate=1' -Invalidate
    }
    Test-WithBackend 0 {
        Test-Client 'current=1 stage=local RPC error=0 candidate=1'
    }
    exit 0
}

Test-Client 'current=1 stage=connect error=2 candidate=0'
Test-WithBackend 0 {
    Test-Client 'current=1 stage=reply error=0 candidate=1'
    Test-Client 'current=1 stage=reply error=0 candidate=1'
}
Test-WithBackend 75 {
    Test-Client 'current=0 stage=reply error=0 candidate=1' -Invalidate
}
Test-WithBackend 1000 {
    Test-Client 'current=1 stage=wait for reply error=1460 candidate=0'
}
Test-WithBackend 0 {
    Test-Client 'current=1 stage=reply error=0 candidate=1'
}
exit 0
