param(
    [string]$Version = '0.2.6',
    [string]$Compiler,
    [string]$InnoCompiler
)

$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Expected a three-part version number, for example 0.1.0: $Version"
}

$buildParameters = @{ Release = $true }
if ($Compiler) {
    $buildParameters.Compiler = $Compiler
}
& (Join-Path $PSScriptRoot 'build.ps1') @buildParameters

$app = Join-Path $PSScriptRoot 'build\Offset Pad.exe'
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "Application was not built: $app"
}
$bytes = [IO.File]::ReadAllBytes($app)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A -or
    $peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length -or
    [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) {
    throw 'The installer requires a Windows x64 executable.'
}

if (-not $InnoCompiler) {
    $candidates = @(
        'E:\Inno-Setup-7\ISCC.exe',
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe')
    )
    $InnoCompiler = $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if (-not $InnoCompiler) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $InnoCompiler = $command.Source }
    }
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup compiler ISCC.exe was not found. Install Inno Setup 7 or pass -InnoCompiler.'
}

$script = Join-Path $PSScriptRoot 'installer\offset-pad.iss'
& $InnoCompiler "/DMyAppVersion=$Version" $script
if ($LASTEXITCODE -ne 0) {
    throw 'Installer build failed.'
}

$installer = Join-Path $PSScriptRoot "dist\Offset-Pad-v$Version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer output was not found: $installer"
}
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$checksum = Join-Path $PSScriptRoot "dist\Offset-Pad-v$Version-windows-x64-setup.sha256"
[IO.File]::WriteAllText($checksum, "$hash  $(Split-Path -Leaf $installer)`n", [Text.Encoding]::ASCII)
Write-Host "Built installer: $installer"
Write-Host "SHA-256: $hash"
