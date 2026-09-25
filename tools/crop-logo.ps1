$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

function Get-VisibleBounds([Drawing.Bitmap]$bitmap) {
    $left = $bitmap.Width
    $top = $bitmap.Height
    $right = -1
    $bottom = -1
    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            if ($bitmap.GetPixel($x, $y).A -gt 0) {
                $left = [Math]::Min($left, $x)
                $top = [Math]::Min($top, $y)
                $right = [Math]::Max($right, $x)
                $bottom = [Math]::Max($bottom, $y)
            }
        }
    }
    if ($right -lt $left) { throw 'The logo has no visible pixels.' }
    return [Drawing.Rectangle]::new($left, $top, $right - $left + 1, $bottom - $top + 1)
}

function Save-Backup([string]$path) {
    $name = [IO.Path]::GetFileNameWithoutExtension($path)
    $extension = [IO.Path]::GetExtension($path)
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Substring(0, 12)
    $backup = Join-Path $buildDirectory "$name-source-$hash$extension"
    if (-not (Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $path -Destination $backup
    }
}

foreach ($name in @('light', 'dark')) {
    $path = Join-Path $projectRoot "media/OffsetPad-$name.ico"
    $bytes = [IO.File]::ReadAllBytes($path)
    if ([BitConverter]::ToUInt16($bytes, 2) -ne 1) { throw "Not an ICO file: $path" }
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $sizes = [Collections.Generic.List[int]]::new()
    $images = [Collections.Generic.List[byte[]]]::new()
    $changed = $false

    for ($index = 0; $index -lt $count; $index++) {
        $entry = 6 + 16 * $index
        $size = if ($bytes[$entry] -eq 0) { 256 } else { [int]$bytes[$entry] }
        $height = if ($bytes[$entry + 1] -eq 0) { 256 } else { [int]$bytes[$entry + 1] }
        if ($size -ne $height) { throw "Non-square ICO frame: $path" }
        $length = [int][BitConverter]::ToUInt32($bytes, $entry + 8)
        $offset = [int][BitConverter]::ToUInt32($bytes, $entry + 12)
        $frameStream = [IO.MemoryStream]::new($bytes, $offset, $length)
        $isPng = $bytes[$offset] -eq 0x89 -and $bytes[$offset + 1] -eq 0x50
        if ($isPng) {
            $source = [Drawing.Bitmap]::new($frameStream)
        } else {
            $single = New-Object byte[] (22 + $length)
            [Array]::Copy($bytes, 0, $single, 0, 4)
            $single[4] = 1
            [Array]::Copy($bytes, $entry, $single, 6, 16)
            [Array]::Copy([BitConverter]::GetBytes([uint32]22), 0, $single, 18, 4)
            [Array]::Copy($bytes, $offset, $single, 22, $length)
            $singleStream = [IO.MemoryStream]::new($single)
            try {
                $icon = [Drawing.Icon]::new($singleStream)
                try { $source = $icon.ToBitmap() } finally { $icon.Dispose() }
            } finally { $singleStream.Dispose() }
        }
        try {
            $crop = Get-VisibleBounds $source
            $scale = [Math]::Min($size / [double]$crop.Width, $size / [double]$crop.Height)
            $drawWidth = [int][Math]::Round($crop.Width * $scale)
            $drawHeight = [int][Math]::Round($crop.Height * $scale)
            $target = [Drawing.Rectangle]::new([int][Math]::Floor(($size - $drawWidth) / 2),
                                               [int][Math]::Floor(($size - $drawHeight) / 2),
                                               $drawWidth, $drawHeight)
            $samePixels = $crop.X -eq $target.X -and $crop.Y -eq $target.Y -and
                          $crop.Width -eq $target.Width -and $crop.Height -eq $target.Height
            if (-not $samePixels) { $changed = $true }
            $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $graphics = [Drawing.Graphics]::FromImage($bitmap)
                try {
                    $graphics.Clear([Drawing.Color]::Transparent)
                    $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
                    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    if ($samePixels) {
                        $graphics.DrawImageUnscaled($source, 0, 0)
                    } else {
                        $graphics.DrawImage($source, $target, $crop, [Drawing.GraphicsUnit]::Pixel)
                    }
                } finally { $graphics.Dispose() }
                $png = [IO.MemoryStream]::new()
                try {
                    $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
                    $images.Add($png.ToArray())
                    $sizes.Add($size)
                } finally { $png.Dispose() }
            } finally { $bitmap.Dispose() }
        } finally {
            $source.Dispose()
            $frameStream.Dispose()
        }
    }

    if (-not $changed) {
        Write-Output "$name ICO already fills its canvas as far as its aspect ratio allows"
        continue
    }
    Save-Backup $path
    $output = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($output)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$count)
        $dataOffset = 6 + 16 * $count
        for ($index = 0; $index -lt $count; $index++) {
            $writer.Write([byte]($sizes[$index] % 256))
            $writer.Write([byte]($sizes[$index] % 256))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$index].Length)
            $writer.Write([uint32]$dataOffset)
            $dataOffset += $images[$index].Length
        }
        foreach ($image in $images) { $writer.Write([byte[]]$image) }
        [IO.File]::WriteAllBytes($path, $output.ToArray())
    } finally {
        $writer.Dispose()
        $output.Dispose()
    }
    Write-Output "Cropped $name ICO without changing its proportions"
}

$pngPath = Join-Path $projectRoot 'media/OffsetPad.png'
$pngStream = [IO.File]::OpenRead($pngPath)
$logo = [Drawing.Bitmap]::new($pngStream)
$pngChanged = $false
$tempPath = Join-Path $buildDirectory 'OffsetPad-logo-cropped.png'
try {
    $crop = Get-VisibleBounds $logo
    if ($crop.Width -ne $logo.Width -or $crop.Height -ne $logo.Height) {
        Save-Backup $pngPath
        $trimmed = $logo.Clone($crop, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $trimmed.Save($tempPath, [Drawing.Imaging.ImageFormat]::Png)
            $pngChanged = $true
        } finally { $trimmed.Dispose() }
    }
} finally {
    $logo.Dispose()
    $pngStream.Dispose()
}
if ($pngChanged) {
    Copy-Item -LiteralPath $tempPath -Destination $pngPath -Force
    Write-Output "Cropped README logo PNG to $($crop.Width)x$($crop.Height)"
}
