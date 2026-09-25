$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

foreach ($name in @('o', '9')) {
    $iconPath = Join-Path $projectRoot "media/OffsetPad-icon-$name.ico"
    $bytes = [IO.File]::ReadAllBytes($iconPath)
    if ([BitConverter]::ToUInt16($bytes, 2) -ne 1) {
        throw "Not an ICO file: $iconPath"
    }
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $sizes = [Collections.Generic.List[int]]::new()
    $largeEntry = -1
    for ($index = 0; $index -lt $count; $index++) {
        $entry = 6 + 16 * $index
        $width = if ($bytes[$entry] -eq 0) { 256 } else { [int]$bytes[$entry] }
        $height = if ($bytes[$entry + 1] -eq 0) { 256 } else { [int]$bytes[$entry + 1] }
        if ($width -ne $height) { throw "Non-square ICO frame: $iconPath" }
        $sizes.Add($width)
        if ($width -eq 256) { $largeEntry = $entry }
    }
    if ($largeEntry -lt 0) { throw "Missing 256-pixel frame: $iconPath" }
    $length = [int][BitConverter]::ToUInt32($bytes, $largeEntry + 8)
    $offset = [int][BitConverter]::ToUInt32($bytes, $largeEntry + 12)
    if ($bytes[$offset] -ne 0x89 -or $bytes[$offset + 1] -ne 0x50) {
        throw "Expected a PNG 256-pixel frame: $iconPath"
    }

    $sourceStream = [IO.MemoryStream]::new($bytes, $offset, $length)
    $source = [Drawing.Bitmap]::new($sourceStream)
    try {
        $left = $source.Width
        $top = $source.Height
        $right = -1
        $bottom = -1
        for ($y = 0; $y -lt $source.Height; $y++) {
            for ($x = 0; $x -lt $source.Width; $x++) {
                if ($source.GetPixel($x, $y).A -gt 0) {
                    $left = [Math]::Min($left, $x)
                    $top = [Math]::Min($top, $y)
                    $right = [Math]::Max($right, $x)
                    $bottom = [Math]::Max($bottom, $y)
                }
            }
        }
        $cropWidth = $right - $left + 1
        $cropHeight = $bottom - $top + 1
        if ($cropWidth -le 0 -or $cropHeight -le 0) {
            throw "Empty 256-pixel frame: $iconPath"
        }
        if ($cropWidth -eq $source.Width -and $cropHeight -eq $source.Height) {
            Write-Output "$name already fills its icon canvas"
            continue
        }

        $hash = (Get-FileHash -LiteralPath $iconPath -Algorithm SHA256).Hash.Substring(0, 12)
        $backupPath = Join-Path $buildDirectory "OffsetPad-icon-$name-source-$hash.ico"
        if (-not (Test-Path -LiteralPath $backupPath)) {
            Copy-Item -LiteralPath $iconPath -Destination $backupPath
        }
        $crop = [Drawing.Rectangle]::new($left, $top, $cropWidth, $cropHeight)
        $images = [Collections.Generic.List[byte[]]]::new()
        foreach ($size in $sizes) {
            $scale = [Math]::Min($size / [double]$cropWidth, $size / [double]$cropHeight)
            $drawWidth = [int][Math]::Round($cropWidth * $scale)
            $drawHeight = [int][Math]::Round($cropHeight * $scale)
            $target = [Drawing.Rectangle]::new([int][Math]::Floor(($size - $drawWidth) / 2),
                                               [int][Math]::Floor(($size - $drawHeight) / 2),
                                               $drawWidth, $drawHeight)
            $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $graphics = [Drawing.Graphics]::FromImage($bitmap)
                try {
                    $graphics.Clear([Drawing.Color]::Transparent)
                    $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
                    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $graphics.DrawImage($source, $target, $crop, [Drawing.GraphicsUnit]::Pixel)
                } finally { $graphics.Dispose() }
                $png = [IO.MemoryStream]::new()
                try {
                    $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
                    $images.Add($png.ToArray())
                } finally { $png.Dispose() }
            } finally { $bitmap.Dispose() }
        }

        $output = [IO.MemoryStream]::new()
        $writer = [IO.BinaryWriter]::new($output)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]$sizes.Count)
            $dataOffset = 6 + 16 * $sizes.Count
            for ($index = 0; $index -lt $sizes.Count; $index++) {
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
            [IO.File]::WriteAllBytes($iconPath, $output.ToArray())
        } finally {
            $writer.Dispose()
            $output.Dispose()
        }
        Write-Output "Cropped $name from ${cropWidth}x$cropHeight to the full icon canvas"
    } finally {
        $source.Dispose()
        $sourceStream.Dispose()
    }
}
