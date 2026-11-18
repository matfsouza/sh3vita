# Renders the boot credits screen and the "v1" label shown on the main menu into 32-bit TGA files for the VPK
# (port/assets/credits/). Usage: powershell -File tools/make_credits.ps1 [-GitHub user] [-Version v1]
# The text is drawn with a Windows font; the port draws the image over a black screen (port/src/rt/sh3_console.c).
param([string]$GitHub = "", [string]$Version = "v1", [string]$Author = "Matheus", [string]$Logo = "")
# -Logo: picture drawn at the top instead of the typed title (default: port/assets/art/logo.* when present)
if (-not $Logo) {
    $found = Get-ChildItem (Join-Path $PSScriptRoot "..\port\assets\art") -Filter "logo.*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $Logo = $found.FullName }
}
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot "..\port\assets\credits"
# with the logo (official artwork) the images go to the non-versioned folder that the VPK build prefers
if ($Logo) { $out = Join-Path $PSScriptRoot "..\port\livearea_user" }
New-Item -ItemType Directory -Force $out | Out-Null

function Save-Tga([System.Drawing.Bitmap]$bmp, [string]$path) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($w * $h * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $hdr = New-Object byte[] 18
    $hdr[2] = 2; $hdr[12] = $w -band 255; $hdr[13] = $w -shr 8; $hdr[14] = $h -band 255; $hdr[15] = $h -shr 8
    $hdr[16] = 32; $hdr[17] = 0x28   # 8 alpha bits, top-left origin; pixels in BGRA order (GDI+ memory layout)
    $fs = [System.IO.File]::Create($path); $fs.Write($hdr, 0, 18); $fs.Write($bytes, 0, $bytes.Length); $fs.Close()
}

function Draw-Lines($g, $lines, [float]$top) {
    $y = $top
    foreach ($l in $lines) {
        $font = New-Object System.Drawing.Font("Georgia", [float]$l[1], [System.Drawing.FontStyle]$l[2], [System.Drawing.GraphicsUnit]::Pixel)
        $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb([int]$l[3], 255, 255, 255))
        $size = $g.MeasureString($l[0], $font)
        $g.DrawString($l[0], $font, $brush, [float]((960 - $size.Width) / 2), $y)
        $y += $size.Height + [float]$l[4]
        $font.Dispose(); $brush.Dispose()
    }
}

$gh = if ($GitHub) { "github.com/$GitHub" } else { "github.com (repository coming soon)" }
# text, size (px), style (0 regular, 1 bold, 2 italic), alpha, space after
$lines = @()
if (-not $Logo) { $lines += ,@("SILENT HILL 3", 40, 1, 255, 0) }
$lines += @(
    @("PlayStation Vita port  -  $Version", 20, 2, 220, 26),
    @("Ported by $Author  -  $gh", 21, 0, 255, 4),
    @("AI-assisted port (Claude, Anthropic)", 18, 2, 230, 16),
    @("Based on sh4vita by vallfrr  -  github.com/vallfrr/sh4vita", 18, 0, 255, 3),
    @("vitaGL, vitaShaRK, SceShaccCgExt  -  Rinnegatamante", 18, 0, 255, 3),
    @("SDL2  -  pl_mpeg (Dominic Szablewski)  -  TLSF (Matthew Conte)", 18, 0, 255, 3),
    @("math-neon  -  VitaSDK and vita-headers contributors", 18, 0, 255, 3),
    @("Vita outline: Tokyoship (CC BY 3.0)  -  Button icons: Kenney (CC0)", 18, 0, 255, 10),
    @("PC fixes reimplemented after the Silent Hill 3 PC Fix and", 18, 2, 240, 0),
    @("ThirteenAG's Widescreen Fix (their behaviour, no code)", 18, 2, 240, 14),
    @("Unofficial fan project, not affiliated with Konami.", 19, 1, 255, 2),
    @("SILENT HILL 3 (c) Konami. No game data is included:", 19, 1, 255, 2),
    @("you need your own copy of the PC version.", 19, 1, 255, 0)
)
$bmp = New-Object System.Drawing.Bitmap 1024, 1024, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$top = 70
if ($Logo) {   # logo 440 px wide, centred, then the text below it
    $img = [System.Drawing.Image]::FromFile($Logo)
    $lw = 440; $lh = [int]($img.Height * $lw / $img.Width)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($img, [int]((960 - $lw) / 2), 40, $lw, $lh)
    $img.Dispose()
    $top = 40 + $lh + 14
}
Draw-Lines $g $lines $top
$g.Dispose()
Save-Tga $bmp (Join-Path $out "credits.tga"); $bmp.Dispose()

$bmp = New-Object System.Drawing.Bitmap 128, 32, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$font = New-Object System.Drawing.Font("Georgia", 18, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 255, 255, 255))
$size = $g.MeasureString($Version, $font)
$g.DrawString($Version, $font, $brush, [float](124 - $size.Width), [float]4)
$g.Dispose()
Save-Tga $bmp (Join-Path $out "version.tga"); $bmp.Dispose()
Write-Output "credits.tga and version.tga written to $out"
