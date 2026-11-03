# Regenerates src/app/why.ico from the artwork in src/app/icon.png.
Add-Type -AssemblyName System.Drawing

$appDir = Join-Path $PSScriptRoot '..\src\app'
$source = New-Object System.Drawing.Bitmap (Join-Path $appDir 'icon.png')

function New-Frame([int] $size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'
    $g.SmoothingMode = 'HighQuality'
    $g.DrawImage($source, 0, 0, $size, $size)
    $g.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return , $stream.ToArray()
}

# The sizes Windows asks for in the taskbar, tray, Explorer and at each display scale.
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$frames = foreach ($size in $sizes) { , (New-Frame $size) }
$source.Dispose()

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16] 0); $writer.Write([uint16] 1); $writer.Write([uint16] $sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte] $size); $writer.Write([byte] $size); $writer.Write([byte] 0); $writer.Write([byte] 0)
    $writer.Write([uint16] 1); $writer.Write([uint16] 32)
    $writer.Write([uint32] $frames[$i].Length); $writer.Write([uint32] $offset)
    $offset += $frames[$i].Length
}
foreach ($frame in $frames) { $writer.Write([byte[]] $frame) }
[System.IO.File]::WriteAllBytes((Join-Path $appDir 'why.ico'), $out.ToArray())
