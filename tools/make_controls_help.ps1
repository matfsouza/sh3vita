# Renders the "Controls" screen shown instead of the game's Button Config (port/src/d3d8/boot_credits.c) into
# port/assets/credits/controls_help.tga (1024x1024, content 960x544). Artwork already packed in
# port/assets/controls/controls.tga: Vita outline by Tokyoship (Wikimedia Commons, CC BY 3.0) and Kenney input
# prompts (CC0), see its README. Layout, colours and labels are drawn here.
# Usage: powershell -File tools/make_controls_help.ps1
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot ".."
$src = Join-Path $root "port\assets\controls\controls.tga"
$out = Join-Path $root "port\assets\credits\controls_help.tga"
$W = 960; $H = 544

# controls.tga: 18-byte header, 1024x512 BGRA, top-left origin
$bytes = [System.IO.File]::ReadAllBytes($src)
$atlas = New-Object System.Drawing.Bitmap 1024, 512, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$lock = $atlas.LockBits((New-Object System.Drawing.Rectangle 0, 0, 1024, 512), [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 18, $lock.Scan0, 1024 * 512 * 4)
$atlas.UnlockBits($lock)

$bmp = New-Object System.Drawing.Bitmap 1024, 1024, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::Black)

# background: dark rust glow in the middle fading to black at the edges
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$path.AddEllipse(-160, -120, $W + 320, $H + 240)
$glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $path
$glow.CenterColor = [System.Drawing.Color]::FromArgb(255, 46, 22, 14)
$glow.SurroundColors = @([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$g.FillRectangle($glow, 0, 0, $W, $H)

$gold = [System.Drawing.Color]::FromArgb(255, 205, 160, 90)
$goldB = New-Object System.Drawing.SolidBrush $gold
$soft = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 232, 226, 214))
$line = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(170, 205, 160, 90)), 1.5
$fTitle = New-Object System.Drawing.Font("Georgia", 30, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fLabel = New-Object System.Drawing.Font("Georgia", 19, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)

# title with thin rules on both sides
$t = "C O N T R O L S"; $ts = $g.MeasureString($t, $fTitle)
$tx = ($W - $ts.Width) / 2; $ty = 30
$g.DrawString($t, $fTitle, $soft, [float]$tx, [float]$ty)
$ry = $ty + $ts.Height / 2 + 1
$g.DrawLine($line, [float]($tx - 150), [float]$ry, [float]($tx - 20), [float]$ry)
$g.DrawLine($line, [float]($tx + $ts.Width + 20), [float]$ry, [float]($tx + $ts.Width + 150), [float]$ry)

# Vita outline, centred, slightly dimmed so the labels lead
$s = 0.52; $ow = 1024 * $s; $oh = 465 * $s; $ox = ($W - $ow) / 2; $oy = 140
$attr = New-Object System.Drawing.Imaging.ImageAttributes
$cm = New-Object System.Drawing.Imaging.ColorMatrix
$cm.Matrix33 = 0.80
$attr.SetColorMatrix($cm)
$dst = New-Object System.Drawing.Rectangle ([int]$ox), ([int]$oy), ([int]$ow), ([int]$oh)
$g.DrawImage($atlas, $dst, 0, 0, 1024, 465, [System.Drawing.GraphicsUnit]::Pixel, $attr)
function P([float]$x, [float]$y) { return New-Object System.Drawing.PointF ([float]($ox + $x * $s)), ([float]($oy + $y * $s)) }

# Kenney prompt icons in the atlas bottom row: 48x48 at x = id*64+8, y = 464
function Icon([int]$id, [float]$x, [float]$y, [float]$size) {
    $r = New-Object System.Drawing.Rectangle ([int]$x), ([int]$y), ([int]$size), ([int]$size)
    $g.DrawImage($atlas, $r, $id * 64 + 8, 464, 48, 48, [System.Drawing.GraphicsUnit]::Pixel)
}
$IC = @{ cross = 0; circle = 1; square = 2; triangle = 3; L = 4; R = 5; start = 6; select = 7; lstick = 8; dpad = 10 }

# side labels: icon + text at a column edge, an elbow leader to the control
$colL = $ox - 26; $colR = $ox + $ow + 26
function Side($icon, [string]$text, [float]$px, [float]$py, [float]$ly, [string]$side) {
    $p = P $px $py
    $ls = $g.MeasureString($text, $fLabel)
    if ($side -eq "L") {
        $ex = $colL; $kx = $p.X - 18
        $g.DrawLine($line, $p.X, $p.Y, [float]$kx, [float]$ly); $g.DrawLine($line, [float]$kx, [float]$ly, [float]$ex, [float]$ly)
        Icon $IC[$icon] ([float]($ex - 34)) ([float]($ly - 15)) 30
        $g.DrawString($text, $fLabel, $soft, [float]($ex - 42 - $ls.Width), [float]($ly - $ls.Height / 2))
    } else {
        $ex = $colR; $kx = $p.X + 18
        $g.DrawLine($line, $p.X, $p.Y, [float]$kx, [float]$ly); $g.DrawLine($line, [float]$kx, [float]$ly, [float]$ex, [float]$ly)
        Icon $IC[$icon] ([float]($ex + 4)) ([float]($ly - 15)) 30
        $g.DrawString($text, $fLabel, $soft, [float]($ex + 42), [float]($ly - $ls.Height / 2))
    }
    $g.FillEllipse($goldB, [float]($p.X - 3.5), [float]($p.Y - 3.5), 7, 7)
}
Side "L" "Camera" 110 30 128 "L"
Side "dpad" "Menus" 140 160 222 "L"
Side "lstick" "Move" 165 272 300 "L"
Side "R" "Ready weapon" 915 30 128 "R"
Side "triangle" "Inventory" 888 122 182 "R"
Side "circle" "Back / Run" 930 162 228 "R"
Side "square" "Flashlight" 850 162 274 "R"
Side "cross" "Action / OK" 888 203 320 "R"
Side "select" "Map" 850 357 366 "R"
Side "start" "Pause" 900 357 412 "R"

# footer: thin rule and "circle Back"
$fy = 500
$g.DrawLine($line, [float]($W / 2 - 220), [float]($fy - 18), [float]($W / 2 + 220), [float]($fy - 18))
$bt = "Back"; $bs = $g.MeasureString($bt, $fLabel)
$bw = 30 + 8 + $bs.Width; $bx = ($W - $bw) / 2
Icon $IC["circle"] ([float]$bx) ([float]($fy - 4)) 30
$g.DrawString($bt, $fLabel, $goldB, [float]($bx + 38), [float]($fy + 11 - $bs.Height / 2))
$g.Dispose()

$data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, 1024, 1024), [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$px = New-Object byte[] (1024 * 1024 * 4)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
$bmp.UnlockBits($data); $bmp.Dispose()
$hdr = New-Object byte[] 18
$hdr[2] = 2; $hdr[13] = 4; $hdr[15] = 4; $hdr[16] = 32; $hdr[17] = 0x28
$fs2 = [System.IO.File]::Create($out); $fs2.Write($hdr, 0, 18); $fs2.Write($px, 0, $px.Length); $fs2.Close()
Write-Output "written $out"
