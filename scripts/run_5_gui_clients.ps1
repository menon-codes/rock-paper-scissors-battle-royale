param(
	[switch]$Build
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repoRoot

if ($Build) {
	Write-Host 'Building binaries...'
	make all
}

$serverPath = Join-Path $repoRoot 'build/bin/server.exe'
$clientPath = Join-Path $repoRoot 'build/bin/client_gui.exe'

if (-not (Test-Path $serverPath)) {
	throw "Missing server binary: $serverPath"
}
if (-not (Test-Path $clientPath)) {
	throw "Missing client binary: $clientPath"
}

$players = @(
	@{ Name = 'alpha'; Choice = 'R'; X = 1; Y = 1 },
	@{ Name = 'bravo'; Choice = 'P'; X = 8; Y = 1 },
	@{ Name = 'charlie'; Choice = 'S'; X = 1; Y = 8 },
	@{ Name = 'delta'; Choice = 'R'; X = 8; Y = 8 },
	@{ Name = 'echo'; Choice = 'P'; X = 5; Y = 5 }
)

Write-Host 'Starting server...'
$serverProc = Start-Process -FilePath $serverPath -WorkingDirectory $repoRoot -PassThru
Start-Sleep -Milliseconds 800

if ($serverProc.HasExited) {
	throw "Server exited immediately with code $($serverProc.ExitCode)."
}

Write-Host "Server PID: $($serverProc.Id)"
Write-Host 'Starting 5 GUI clients in auto mode...'

$clientProcs = @()
foreach ($p in $players) {
	$launchSpec = "{0} {1} {2} {3}" -f $p.Name, $p.Choice, $p.X, $p.Y
	$proc = Start-Process -FilePath $clientPath -ArgumentList $launchSpec -WorkingDirectory $repoRoot -PassThru
	$clientProcs += $proc
	Write-Host ("Client PID {0}: {1} {2} at ({3},{4})" -f $proc.Id, $p.Name, $p.Choice, $p.X, $p.Y)
	Start-Sleep -Milliseconds 200
}

Write-Host ''
Write-Host 'Launched all processes.'
Write-Host 'To stop everything quickly in PowerShell:'
Write-Host ("  Stop-Process -Id {0},{1}" -f $serverProc.Id, (($clientProcs | ForEach-Object { $_.Id }) -join ','))
