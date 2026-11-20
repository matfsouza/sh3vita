# Renders the background of the one-time "Preparing graphics" screen into ui/prep.tga (960x544, 32-bit, top-left).
# port/src/vita/main.c prep_progress draws it and fills the progress bar inside the empty track: x 562..898,
# y 354..360 from the top (keep both in step).
# Artwork: port/assets/art/loading.jpg (the loading-screen picture) and logo.png. With them (official artwork) the file
# goes to the non-versioned port/livearea_user, which the VPK build prefers; without them, a plain typed version.
# -Loading: the same screen without the progress block, as the boot screen ui/loading.tga (vita boot_credits.c shows it
# until the first logo).
# Usage: powershell -File tools/make_prep_screen.ps1 [-Loading] [-Repo matfsouza/sh3vita] [-Author Matheus]
param([switch]$Loading, [string]$Repo = "matfsouza/sh3vita", [string]$Author = "Matheus")
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot ".."
$art = Join-Path $root "port\assets\art"
$logoPath = Join-Path $art "logo.png"; $bgPath = Join-Path $art "loading.jpg"
$hasArt = (Test-Path $logoPath) -and (Test-Path $bgPath)
$outDir = if ($hasArt) { Join-Path $root "port\livearea_user" } else { Join-Path $root "port\assets\credits" }
New-Item -ItemType Directory -Force $outDir | Out-Null
$out = Join-Path $outDir $(if ($Loading) { "loading.tga" } else { "prep.tga" })
$W = 960; $H = 544

$bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::Black)
function C([int]$a, [int]$r, [int]$gg, [int]$b) { return [System.Drawing.Color]::FromArgb($a, $r, $gg, $b) }
function B($c) { return New-Object System.Drawing.SolidBrush $c }
function F([float]$px, $style = [System.Drawing.FontStyle]::Regular) { return New-Object System.Drawing.Font("Georgia", $px, $style, [System.Drawing.GraphicsUnit]::Pixel) }
function PF([float]$x, [float]$y) { return New-Object System.Drawing.PointF $x, $y }
function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90); $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90); $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure(); return $p
}
$rnd = New-Object System.Random 3   # fixed seed: the same picture every time

# 1. the artwork, Heather on the left, bleeding to the edges
if ($hasArt) {
    $bg = [System.Drawing.Image]::FromFile((Resolve-Path $bgPath))
    $s = $H / $bg.Height; $bw = $bg.Width * $s
    $g.DrawImage($bg, [float](-230), 0, [float]$bw, [float]$H)
}
# 2. darken towards the right for the text column, then a vignette
$fade = New-Object System.Drawing.Drawing2D.LinearGradientBrush (PF 330 0), (PF 640 0), (C 0 0 0 0), (C 235 6 3 2)
$g.FillRectangle($fade, 330, 0, 310, $H)
$g.FillRectangle((B (C 235 6 3 2)), 639, 0, $W - 639, $H)
$vp = New-Object System.Drawing.Drawing2D.GraphicsPath
$vp.AddEllipse(-200, -140, $W + 400, $H + 280)
$vig = New-Object System.Drawing.Drawing2D.PathGradientBrush $vp
$vig.CenterColor = C 0 0 0 0; $vig.SurroundColors = @((C 200 0 0 0))
$g.FillRectangle($vig, 0, 0, $W, $H)

# 3. the column: everything aligned on x0
$x0 = 562; $cw = 336
$gold = C 255 214 168 96
$cream = B (C 255 238 230 216); $goldB = B $gold; $muted = B (C 255 170 156 140); $dim = B (C 255 120 106 94)
# red glow behind the logo, like the light bleeding through the artwork
$gp = New-Object System.Drawing.Drawing2D.GraphicsPath
$gp.AddEllipse($x0 - 60, 20, $cw + 120, 170)
$rg = New-Object System.Drawing.Drawing2D.PathGradientBrush $gp
$rg.CenterColor = C 110 150 24 12; $rg.SurroundColors = @((C 0 0 0 0))
$g.FillRectangle($rg, $x0 - 60, 20, $cw + 120, 170)
if ($hasArt) {
    $logo = [System.Drawing.Image]::FromFile((Resolve-Path $logoPath))
    $lh = $cw * $logo.Height / $logo.Width
    $g.DrawImage($logo, $x0, 58, $cw, [float]$lh)
    $ly = 58 + $lh
} else {
    $g.DrawString("SILENT HILL 3", (F 44), $cream, $x0, 52)
    $g.DrawString("E N H A N C E D   E D I T I O N", (F 16), $muted, $x0 + 2, 104)
    $ly = 126
}
$g.DrawString("PlayStation Vita port", (F 15 ([System.Drawing.FontStyle]::Italic)), $goldB, $x0 + 1, $ly + 8)

# author block: a hairline on the left edge, small caps label, the name and the GitHub big
$ay = 196
$g.FillRectangle((B (C 200 214 168 96)), $x0, $ay + 4, 2, 92)
$g.DrawString("P O R T E D   B Y", (F 12), $muted, $x0 + 16, $ay)
$g.DrawString($Author, (F 34), $cream, $x0 + 12, $ay + 16)
$url = "github.com/$Repo"
$uf = F 25
while ($g.MeasureString($url, $uf).Width -gt $cw - 10) { $uf = F ($uf.Size - 0.5) }
$g.DrawString($url, $uf, $goldB, $x0 + 14, $ay + 62)

# progress: label, track (the port fills it), note
if (-not $Loading) {
    $g.DrawString("Preparing graphics", (F 16), $cream, $x0, 322)
    $g.FillPath((B (C 255 28 16 12)), (RoundRect $x0 352 $cw 10 5))
    $g.DrawPath((New-Object System.Drawing.Pen (C 110 214 168 96), 1), (RoundRect $x0 352 $cw 10 5))
    $g.DrawString("Only the first time the game starts", (F 13), $muted, $x0, 369)
}

# where to report problems (players who got the game from a homebrew store may not know the repository)
$iy = if ($Loading) { 344 } else { 402 }
$g.FillRectangle((B (C 200 214 168 96)), $x0, $iy + 3, 2, 34)
$g.DrawString("Found a problem? Please report it as an issue:", (F 14), $cream, $x0 + 12, $iy)
$g.DrawString("$url/issues", (F 14), $goldB, $x0 + 12, $iy + 19)

# notice
$dot = [string][char]0x00B7; $copy = [string][char]0x00A9
$g.DrawString("Unofficial fan port. Not affiliated with", (F 14), $muted, $x0, 458)
$g.DrawString("or endorsed by Konami.", (F 14), $muted, $x0, 476)
$g.DrawString("SILENT HILL 3 $copy Konami  $dot  needs your own copy of the PC game", (F 11), $dim, $x0, 500)

# 4. texture like the logo's: thin scratches and film grain over everything
$scratch = New-Object System.Drawing.Pen (C 40 255 230 210), 1
for ($i = 0; $i -lt 26; $i++) {
    $sx = $rnd.Next(0, $W); $sy = $rnd.Next(0, $H); $len = $rnd.Next(12, 70)
    $g.DrawLine($scratch, [float]$sx, [float]$sy, [float]($sx + $rnd.Next(-6, 7)), [float]($sy + $len))
}
$g.Dispose()

$data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $W, $H), [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$px = New-Object byte[] ($W * $H * 4)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
for ($i = 0; $i -lt $px.Length; $i += 4) {   # grain: the same small offset on B, G, R
    $n = $rnd.Next(-9, 10)
    for ($c = 0; $c -lt 3; $c++) { $v = $px[$i + $c] + $n; if ($v -lt 0) { $v = 0 } elseif ($v -gt 255) { $v = 255 }; $px[$i + $c] = [byte]$v }
    $px[$i + 3] = 255
}
$bmp.UnlockBits($data); $bmp.Dispose()
$hdr = New-Object byte[] 18
$hdr[2] = 2; $hdr[12] = $W -band 0xff; $hdr[13] = $W -shr 8; $hdr[14] = $H -band 0xff; $hdr[15] = $H -shr 8; $hdr[16] = 32; $hdr[17] = 0x28
$fs = [System.IO.File]::Create($out); $fs.Write($hdr, 0, 18); $fs.Write($px, 0, $px.Length); $fs.Close()
Write-Output "written $out"
