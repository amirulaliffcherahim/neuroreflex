# Quick design look: shots of intro (first run and with a normal), wait, GO,
# feedback, and the three result pages after a shortened session.
# Seeds history so the result is judged against a normal. Not a test: see drive.ps1.
param([string]$OutDir = "$PSScriptRoot\look", [int[]]$History = @(300, 296, 305, 298, 302), [int]$Reaction = 345)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WL {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  public struct R { public int L, T, Rt, B; } public struct P { public int X, Y; }
}
"@
$exe = Resolve-Path "$PSScriptRoot\..\..\Software\Apps\LVGL-GUI\simulator\build\bin\NeuroReflexSimulator.exe"
$fsRoot = Join-Path (Resolve-Path "$PSScriptRoot\..\..\Software") "Output"
New-Item -ItemType Directory -Force $OutDir, $fsRoot | Out-Null
Remove-Item "$OutDir\*.png" -ErrorAction SilentlyContinue

function Seed([int[]]$h) {
    Remove-Item "$fsRoot\history.bin" -ErrorAction SilentlyContinue
    if (-not $h.Count) { return }
    $ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([uint32]0x3152524E); $bw.Write([uint32]$h.Count)
    for ($i = 0; $i -lt 14; $i++) {
        $m = if ($i -lt $h.Count) { $h[$i] } else { 0 }
        $st = if ($i -lt $h.Count) { $avg = ($h | Measure-Object -Average).Average; if ($m -gt $avg * 1.08) { 4 } elseif ($m -lt $avg * 0.96) { 2 } else { 3 } } else { 0 }
        $bw.Write([uint16]$m); $bw.Write([byte]0); $bw.Write([byte]$st)
    }
    [IO.File]::WriteAllBytes("$fsRoot\history.bin", $ms.ToArray())
}
function Launch {
    $script:p = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $script:h = [IntPtr]::Zero
    for ($i = 0; $i -lt 50 -and $script:h -eq [IntPtr]::Zero; $i++) { Start-Sleep -m 200; $script:p.Refresh(); $script:h = $script:p.MainWindowHandle }
    Start-Sleep -m 1200
}
function Grab {
    [void][WL]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 3); Start-Sleep -m 50
    $r = New-Object WL+R; [void][WL]::GetClientRect($h, [ref]$r); $pt = New-Object WL+P; [void][WL]::ClientToScreen($h, [ref]$pt)
    $b = New-Object Drawing.Bitmap $r.Rt, $r.B; $g = [Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object Drawing.Size $r.Rt, $r.B)); $g.Dispose(); $b
}
function Shot($n) { $b = Grab; $b.Save("$OutDir\$n.png"); $b.Dispose() }
function Green { $b = Grab; $c = $b.GetPixel([int]($b.Width * 0.24), [int]($b.Height * 0.52)); $b.Dispose(); $c.G -gt 150 -and $c.R -lt 90 }
function Tap([char]$k, [int]$hold = 40) {
    $vk = [uint32][char]::ToUpper($k); $sc = [WL]::MapVirtualKey($vk, 0)
    [void][WL]::PostMessage($h, 0x100, [IntPtr][int]$vk, [IntPtr](1 -bor ($sc -shl 16))); Start-Sleep -m $hold
    [void][WL]::PostMessage($h, 0x101, [IntPtr][int]$vk, [IntPtr]([int64](1 -bor ($sc -shl 16) -bor 0xC0000000)))
}

# 1. First run: no history.
Seed @(); Launch; Shot "00_intro_first"; $p.Kill(); Start-Sleep -m 400

# 2. With a normal: intro, a session, every result page.
Seed $History; Launch
Shot "01_intro"
Tap '3'; Start-Sleep -m 900; Shot "02_wait"
$t0 = Get-Date; $n = 0; $prev = $false
while (((Get-Date) - $t0).TotalSeconds -lt 200) {
    Start-Sleep -m 20
    $gr = Green
    if ($gr -and -not $prev) {
        if ($n -eq 0) { Start-Sleep -m 150; Shot "03_go" }
        Start-Sleep -m ([Math]::Max(0, $Reaction - 60 - $(if ($n -eq 0) { 210 } else { 0 })))
        Tap '2' 30; $n++
        if ($n -eq 1) { Start-Sleep -m 150; Shot "04_feedback" }
    }
    $prev = $gr
    if (((Get-Date) - $t0).TotalSeconds -gt 182) {
        $b = Grab; $c = $b.GetPixel(320, 160); $b.Dispose()
        if ($c.R + $c.G + $c.B -gt 250 -and -not (Green)) { }
    }
}
Start-Sleep -m 1500
Shot "05_result"
Tap '2'; Start-Sleep -m 400; Shot "06_trend"
Tap '2'; Start-Sleep -m 400; Shot "07_details"
$p.Kill()
"answered $n"
