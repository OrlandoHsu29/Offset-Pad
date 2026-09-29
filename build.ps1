param([string]$Compiler, [switch]$Release)

$ErrorActionPreference = 'Stop'

if ($Compiler) {
    if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
        throw "Compiler not found: $Compiler"
    }
    $compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
} else {
    $command = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $command) {
        $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vsPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
            if ($vsPath) {
                $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
                if (Test-Path -LiteralPath $vcvars) {
                    $output = Join-Path $PSScriptRoot 'build'
                    New-Item -ItemType Directory -Path $output -Force | Out-Null
                    $options = if ($Release) { '/O1 /DNDEBUG' } else { '/Od /Zi' }
                    $commandLine = 'call "{0}" >nul && rc /nologo /fo "build\app-icon.res" "resources\app-icon.rc" && cl /nologo /std:c11 /utf-8 /W4 {1} /DUNICODE /D_UNICODE /I "src\app" /I "src\app\ui" /I "src\input" /Fd:"build\Offset Pad-compiler.pdb" "src\app\main.c" "src\app\app_settings.c" "src\app\ui\app_ui.c" "src\app\ui\app_ui_paint.c" "src\app\ui\app_tray.c" "src\app\update_check.c" "src\app\ui\rounded_box.c" "src\input\keymap.c" "build\app-icon.res" /Fe:"build\Offset Pad.exe" /Fo:"build\\" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib gdi32.lib dwmapi.lib gdiplus.lib winhttp.lib comctl32.lib' -f $vcvars, $options
                    Push-Location $PSScriptRoot
                    try {
                        & cmd.exe /d /c $commandLine
                        if ($LASTEXITCODE -ne 0) { throw 'Offset Pad MSVC build failed.' }
                    } finally {
                        Pop-Location
                    }
                    Write-Host "Built $(Join-Path $output 'Offset Pad.exe')"
                    return
                }
            }
        }
        throw 'No Windows C compiler found. Pass -Compiler with the path to MinGW gcc.exe.'
    }
    $compilerPath = $command.Source
}

$target = (& $compilerPath -dumpmachine).Trim()
if ($LASTEXITCODE -ne 0 -or $target -notmatch '(mingw|windows)') {
    throw "A Windows MinGW GCC is required. Found target: $target"
}

$windres = Join-Path (Split-Path -Parent $compilerPath) 'windres.exe'
if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) {
    $command = Get-Command windres -ErrorAction SilentlyContinue
    if (-not $command) {
        throw 'windres.exe was not found next to gcc.exe or in PATH.'
    }
    $windres = $command.Source
}

$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$resource = Join-Path $output 'app-icon.o'
& $windres -i (Join-Path $PSScriptRoot 'resources\app-icon.rc') -o $resource -I $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Icon resource compilation failed.' }

$sources = @(
    (Join-Path $PSScriptRoot 'src\app\main.c'),
    (Join-Path $PSScriptRoot 'src\app\app_settings.c'),
    (Join-Path $PSScriptRoot 'src\app\ui\app_ui.c'),
    (Join-Path $PSScriptRoot 'src\app\ui\app_ui_paint.c'),
    (Join-Path $PSScriptRoot 'src\app\ui\app_tray.c'),
    (Join-Path $PSScriptRoot 'src\app\update_check.c'),
    (Join-Path $PSScriptRoot 'src\app\ui\rounded_box.c'),
    (Join-Path $PSScriptRoot 'src\input\keymap.c')
)
$options = if ($Release) { @('-Os', '-s') } else { @('-O0', '-g') }
$destination = Join-Path $output 'Offset Pad.exe'
& $compilerPath -std=c11 -finput-charset=UTF-8 -DUNICODE -D_UNICODE -Wall -Wextra @options `
    '-Isrc/app' '-Isrc/app/ui' '-Isrc/input' @sources $resource -mwindows -o $destination `
    -luser32 -lshell32 -ladvapi32 -lgdi32 -ldwmapi -lgdiplus -lwinhttp -lcomctl32
if ($LASTEXITCODE -ne 0) { throw 'Offset Pad build failed.' }
Write-Host "Built $destination"
