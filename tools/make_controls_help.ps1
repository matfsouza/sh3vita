# Renders the "Controls" screen shown instead of the game's Button Config (port/src/d3d8/boot_credits.c), one picture
# per game language and control layout (port/src/dx/control_scheme.h), into
# port/assets/credits/controls_<language>_<layout>.tga (960x544, 32-bit, top-left origin). Text: tools/controls_text.json.
# Artwork already packed in port/assets/controls/controls.tga: Vita outline by Tokyoship (Wikimedia Commons, CC BY 3.0)
# and Kenney input prompts (CC0), see its README. Layout, colours and labels are drawn here.
# Usage: powershell -File tools/make_controls_help.ps1 (writes all 8 x 3 pictures)
param([int]$Scheme = -1, [int]$Lang = -1)
if ($Scheme -lt 0 -or $Lang -lt 0) {
    foreach ($l in 0..7) { foreach ($s in 0, 1, 2) { & powershell -NoProfile -File $PSCommandPath -Scheme $s -Lang $l } }
    exit
}
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot ".."
$src = Join-Path $root "port\assets\controls\controls.tga"
# file number = the game's language value (byte 0x70e66b1): 0 Japanese, 1 English, 2 French ... 7 Chinese;
# controls_text.json lists English first, so its entry n is game value (n + 1) mod 8
# with the wallpaper (official artwork) the pictures go to the non-versioned port/livearea_user, which the VPK prefers
$outDir = if (Test-Path (Join-Path $root "port\assets\art\controls_wallpaper.png")) { "port\livearea_user" } else { "port\assets\credits" }
$out = Join-Path $root "$outDir\controls_$((($Lang + 1) % 8))_$Scheme.tga"
$T = (Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "controls_text.json") | ConvertFrom-Json).languages[$Lang]
$W = 960; $H = 544

# controls.tga: 18-byte header, 1024x512 BGRA, top-left origin
$bytes = [System.IO.File]::ReadAllBytes($src)
$atlas = New-Object System.Drawing.Bitmap 1024, 512, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$lock = $atlas.LockBits((New-Object System.Drawing.Rectangle 0, 0, 1024, 512), [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 18, $lock.Scan0, 1024 * 512 * 4)
$atlas.UnlockBits($lock)

$bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::Black)

function C([int]$a, [int]$r, [int]$gg, [int]$b) { return [System.Drawing.Color]::FromArgb($a, $r, $gg, $b) }
function Brush($c) { return New-Object System.Drawing.SolidBrush $c }
function F([string]$name, [float]$px) { return New-Object System.Drawing.Font($name, $px, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel) }
function PF([float]$x, [float]$y) { return New-Object System.Drawing.PointF $x, $y }

# background: the wallpaper (port/assets/art/controls_wallpaper.png, official artwork, not versioned), softened and
# darkened so the labels stay readable; without it, a low rust glow behind the console
$wallPath = Join-Path $root "port\assets\art\controls_wallpaper.png"
if (Test-Path $wallPath) {
    $wall = [System.Drawing.Image]::FromFile((Resolve-Path $wallPath))
    $small = New-Object System.Drawing.Bitmap 400, 227   # soften: down to ~40%, back up with bilinear filtering
    $sg = [System.Drawing.Graphics]::FromImage($small)
    $sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
    $sg.DrawImage($wall, 0, 0, 400, 227); $sg.Dispose()
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
    $g.DrawImage($small, -2, -2, $W + 4, $H + 4)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (C 150 6 3 2)), 0, 0, $W, $H)
    $vp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $vp.AddEllipse(-160, -100, $W + 320, $H + 200)
    $vig = New-Object System.Drawing.Drawing2D.PathGradientBrush $vp
    $vig.CenterColor = C 60 0 0 0; $vig.SurroundColors = @((C 190 0 0 0))
    $g.FillRectangle($vig, 0, 0, $W, $H)
    $rnd = New-Object System.Random (11 + $Scheme)   # thin scratches, as on the loading screen
    $scratch = New-Object System.Drawing.Pen (C 34 255 230 210), 1
    for ($i = 0; $i -lt 22; $i++) {
        $sx = $rnd.Next(0, $W); $sy = $rnd.Next(0, $H); $len = $rnd.Next(12, 70)
        $g.DrawLine($scratch, [float]$sx, [float]$sy, [float]($sx + $rnd.Next(-6, 7)), [float]($sy + $len))
    }
} else {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse(-120, -60, $W + 240, $H + 160)
    $glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $path
    $glow.CenterPoint = PF ($W / 2) 270
    $glow.CenterColor = C 255 40 20 13
    $glow.SurroundColors = @((C 255 0 0 0))
    $g.FillRectangle($glow, 0, 0, $W, $H)
}

$gold = C 255 214 168 96
$goldB = Brush $gold
$cream = Brush (C 255 236 230 218)
$dim = Brush (C 255 118 104 90)
$muted = Brush (C 255 176 164 148)
$rule = New-Object System.Drawing.Pen (C 120 214 168 96), 1
$leader = New-Object System.Drawing.Pen (C 150 214 168 96), 1.25
$face = $T.font
$cjk = $face -ne "Georgia"
$k = if ($cjk) { 0.9 } else { 1.0 }   # the CJK faces look larger at the same pixel size
$fTitle = F $face (15 * $k)
$fTab = F $face (17 * $k)
$fDesc = F $face (17 * $k)
$fLabel = F $face (18 * $k)
$fKey = F "Georgia" 26
$fSmall = F $face (15 * $k)
# the same font, made smaller until the text fits in maxw pixels
function Fit([string]$text, $font, [float]$maxw) {
    $f = $font
    while ($g.MeasureString($text, $f).Width -gt $maxw -and $f.Size -gt 10) { $f = F $f.FontFamily.Name ($f.Size - 0.5) }
    return $f
}

function Centre([string]$text, $font, $brush, [float]$y, [float]$cx = $W / 2, [float]$maxw = 900) {
    $font = Fit $text $font $maxw
    $m = $g.MeasureString($text, $font)
    $g.DrawString($text, $font, $brush, [float]($cx - $m.Width / 2), [float]$y)
    return $m
}

# title: small, spaced, with a short rule on each side
$m = Centre $T.title $fTitle $muted 16
$ty = 16 + $m.Height / 2
$g.DrawLine($rule, [float]($W / 2 - $m.Width / 2 - 70), [float]$ty, [float]($W / 2 - $m.Width / 2 - 14), [float]$ty)
$g.DrawLine($rule, [float]($W / 2 + $m.Width / 2 + 14), [float]$ty, [float]($W / 2 + $m.Width / 2 + 70), [float]$ty)

# layout tabs: the chosen one bright and underlined, the others dim; arrows at both ends (left / right change it)
$tabs = $T.tabs
$tabX = @(250, 480, 710); $tabY = 46
for ($i = 0; $i -lt 3; $i++) {
    $b = if ($i -eq $Scheme) { $cream } else { $dim }
    $tm = Centre $tabs[$i] $fTab $b $tabY $tabX[$i] 215
    if ($i -eq $Scheme) {
        $u = New-Object System.Drawing.Pen $gold, 2
        $g.DrawLine($u, [float]($tabX[$i] - $tm.Width / 2 + 4), [float]($tabY + $tm.Height + 3), [float]($tabX[$i] + $tm.Width / 2 - 4), [float]($tabY + $tm.Height + 3))
    }
}
$ay = $tabY + 11
$g.FillPolygon($goldB, @((PF 100 $ay), (PF 112 ($ay - 7)), (PF 112 ($ay + 7))))
$g.FillPolygon($goldB, @((PF 860 $ay), (PF 848 ($ay - 7)), (PF 848 ($ay + 7))))
$g.DrawLine($rule, 120, [float]($tabY + 34), 840, [float]($tabY + 34))

# one plain sentence: what this layout is for
Centre $T.desc[$Scheme] $fDesc $goldB 94 | Out-Null

# Vita outline, centred, dimmed so the labels lead
# one axis for the whole screen: the console is centred on x = 480 like the title, tabs, sentence, L1/R1 and footer
$s = 0.44; $ow = 1024 * $s; $oh = 465 * $s; $ox = ($W - $ow) / 2; $oy = 156
$attr = New-Object System.Drawing.Imaging.ImageAttributes
$cm = New-Object System.Drawing.Imaging.ColorMatrix
$cm.Matrix33 = 0.70
$attr.SetColorMatrix($cm)
$dst = New-Object System.Drawing.Rectangle ([int]$ox), ([int]$oy), ([int]$ow), ([int]$oh)
# the console's screen filled dark, so the wallpaper does not show through the drawing
$g.FillRectangle((New-Object System.Drawing.SolidBrush (C 215 8 5 4)), [float]($ox + 238 * $s), [float]($oy + 54 * $s), [float](556 * $s), [float](316 * $s))
$g.DrawImage($atlas, $dst, 0, 0, 1024, 465, [System.Drawing.GraphicsUnit]::Pixel, $attr)
function P([float]$x, [float]$y) { return PF ($ox + $x * $s) ($oy + $y * $s) }

# touch halves: drawn on the screen (screen touch) or on a small "back of the Vita" panel (rear touch)
function Halves([float]$x0, [float]$y0, [float]$x1, [float]$y1, [string]$caption) {
    $mid = ($x0 + $x1) / 2
    $fill = Brush (C 70 214 168 96)
    $edge = New-Object System.Drawing.Pen (C 220 214 168 96), 1.5
    $g.FillRectangle($fill, [float]$x0, [float]$y0, [float]($mid - $x0 - 3), [float]($y1 - $y0))
    $g.FillRectangle($fill, [float]($mid + 3), [float]$y0, [float]($x1 - $mid - 3), [float]($y1 - $y0))
    $g.DrawRectangle($edge, [float]$x0, [float]$y0, [float]($mid - $x0 - 3), [float]($y1 - $y0))
    $g.DrawRectangle($edge, [float]($mid + 3), [float]$y0, [float]($x1 - $mid - 3), [float]($y1 - $y0))
    # "L1" over its caption, the pair centred on the half by the glyphs' own heights (GDI+ text boxes carry empty
    # space above the letters: ~0.22 of the font size)
    $cy = ($y0 + $y1) / 2
    $km = $g.MeasureString("L1", $fKey)
    $kTop = $fKey.Size * 0.22; $kGlyph = $fKey.Size * 0.72; $sTop = $fSmall.Size * 0.22; $sGlyph = $fSmall.Size * 0.75
    $gap = 5; $block = $kGlyph + $gap + $sGlyph
    $ky = $cy - $block / 2 - $kTop
    $sy = $cy - $block / 2 + $kGlyph + $gap - $sTop
    $lx = ($x0 + $mid) / 2; $rx = ($mid + $x1) / 2; $half = $mid - $x0 - 12
    $g.DrawString("L1", $fKey, $cream, [float]($lx - $km.Width / 2), [float]$ky)
    $g.DrawString("R1", $fKey, $cream, [float]($rx - $km.Width / 2), [float]$ky)
    Centre $T.left $fSmall $goldB $sy $lx $half | Out-Null
    Centre $T.right $fSmall $goldB $sy $rx $half | Out-Null
    if ($caption) { Centre $caption $fSmall $muted ($y1 + 4) $mid | Out-Null }
}

# Kenney prompt icons in the atlas bottom row: 48x48 at x = id*64+8, y = 464
function Icon([int]$id, [float]$x, [float]$y, [float]$size) {
    $r = New-Object System.Drawing.Rectangle ([int]$x), ([int]$y), ([int]$size), ([int]$size)
    $g.DrawImage($atlas, $r, $id * 64 + 8, 464, 48, 48, [System.Drawing.GraphicsUnit]::Pixel)
}
$IC = @{ cross = 0; circle = 1; square = 2; triangle = 3; L = 4; R = 5; start = 6; select = 7; lstick = 8; dpad = 10 }

# side labels: icon + text at a column edge, an elbow leader to the control
$colL = $ox - 18; $colR = $ox + $ow + 18   # mirrored: the same gap from the console on both sides
function Side($icon, [string]$text, [float]$px, [float]$py, [float]$ly, [string]$side) {
    $p = P $px $py
    if ($side -eq "L") {
        $f = Fit $text $fLabel ($colL - 40 - 8)
        $ls = $g.MeasureString($text, $f)
        $ex = $colL; $kx = $p.X - 16
        $g.DrawLine($leader, $p.X, $p.Y, [float]$kx, [float]$ly); $g.DrawLine($leader, [float]$kx, [float]$ly, [float]$ex, [float]$ly)
        Icon $IC[$icon] ([float]($ex - 32)) ([float]($ly - 14)) 28
        $g.DrawString($text, $f, $cream, [float]($ex - 40 - $ls.Width), [float]($ly - $ls.Height / 2))
    } else {
        $f = Fit $text $fLabel ($W - $colR - 40 - 8)
        $ls = $g.MeasureString($text, $f)
        $ex = $colR; $kx = $p.X + 16
        $g.DrawLine($leader, $p.X, $p.Y, [float]$kx, [float]$ly); $g.DrawLine($leader, [float]$kx, [float]$ly, [float]$ex, [float]$ly)
        Icon $IC[$icon] ([float]($ex + 4)) ([float]($ly - 14)) 28
        $g.DrawString($text, $f, $cream, [float]($ex + 40), [float]($ly - $ls.Height / 2))
    }
    $g.FillEllipse($goldB, [float]($p.X - 3), [float]($p.Y - 3), 6, 6)
}

# the same game command keeps the same name in every layout; only the button changes
# one label size for both columns: the largest that fits the longest label of this language and layout, so no
# single label shrinks on its own
$fFoot = $fLabel   # the footer keeps the full size
$labels = @($T.search, $T.dpad, $T.move, $T.weapon, $T.items, $T.run, $T.light, $T.lightback, $T.action, $T.map, $T.pause)
$room = [Math]::Min($colL - 40 - 8, $W - $colR - 40 - 8)
foreach ($lbl in $labels) { $fLabel = Fit $lbl $fLabel $room }   # not $t: PowerShell names ignore case ($T = texts)
# rows on one 40-px rhythm (146, 186 ... 386), shared by both columns
Side "L" $T.search 110 30 146 "L"
Side "dpad" $T.dpad 140 160 226 "L"
Side "lstick" $T.move 165 272 306 "L"
Side "R" $T.weapon 915 30 146 "R"
if ($Scheme -eq 0) {
    Side "triangle" $T.items 888 122 186 "R"
    Side "circle" $T.run 930 162 226 "R"
    Side "square" $T.light 850 162 266 "R"
    Side "cross" $T.action 888 203 306 "R"
    Side "select" $T.map 850 357 346 "R"
} else {
    Side "triangle" $T.map 888 122 186 "R"
    Side "circle" $T.lightback 930 162 226 "R"
    Side "square" $T.run 850 162 266 "R"
    Side "cross" $T.action 888 203 306 "R"
    Side "select" $T.items 850 357 346 "R"
}
Side "start" $T.pause 900 357 386 "R"

# the touch part of the layout
if ($Scheme -eq 2) {
    $a = P 244 60; $b = P 788 364
    Halves $a.X $a.Y $b.X $b.Y ""
    Centre $T.turn $fDesc $cream 420 | Out-Null
} elseif ($Scheme -eq 1) {
    $px0 = $W / 2 - 160; $px1 = $W / 2 + 160; $py0 = 400; $py1 = 454
    Halves $px0 $py0 $px1 $py1 ""
    Centre $T.rear $fSmall $muted 459 | Out-Null
} else {
    Centre $T.nosteps $fDesc $muted 420 | Out-Null
}

# footer: thin rule, "d-pad Change layout" and "circle Back"
$fy = 508
$g.DrawLine($rule, [float]($W / 2 - 260), [float]($fy - 14), [float]($W / 2 + 260), [float]($fy - 14))
$ct = $T.change; $cs = $g.MeasureString($ct, $fFoot)
$bt = $T.back; $bs = $g.MeasureString($bt, $fFoot)
$tw = 28 + 8 + $cs.Width + 48 + 28 + 8 + $bs.Width; $bx = ($W - $tw) / 2
Icon $IC["dpad"] ([float]$bx) ([float]($fy - 2)) 28
$g.DrawString($ct, $fFoot, $goldB, [float]($bx + 36), [float]($fy + 12 - $cs.Height / 2))
$bx2 = $bx + 36 + $cs.Width + 48
Icon $IC["circle"] ([float]$bx2) ([float]($fy - 2)) 28
$g.DrawString($bt, $fFoot, $goldB, [float]($bx2 + 36), [float]($fy + 12 - $bs.Height / 2))
$g.Dispose()

$data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $W, $H), [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$px = New-Object byte[] ($W * $H * 4)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
$bmp.UnlockBits($data); $bmp.Dispose()
if ($rnd) {   # film grain over the wallpaper version: the same small offset on B, G, R
    for ($i = 0; $i -lt $px.Length; $i += 4) {
        $n = $rnd.Next(-7, 8)
        for ($c = 0; $c -lt 3; $c++) { $v = $px[$i + $c] + $n; if ($v -lt 0) { $v = 0 } elseif ($v -gt 255) { $v = 255 }; $px[$i + $c] = [byte]$v }
    }
}
$hdr = New-Object byte[] 18
$hdr[2] = 2; $hdr[12] = $W -band 0xff; $hdr[13] = $W -shr 8; $hdr[14] = $H -band 0xff; $hdr[15] = $H -shr 8; $hdr[16] = 32; $hdr[17] = 0x28
$fs2 = [System.IO.File]::Create($out); $fs2.Write($hdr, 0, 18); $fs2.Write($px, 0, $px.Length); $fs2.Close()
Write-Output "written $out"
