param([string]$Destination = (Join-Path $PSScriptRoot 'Assets'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
foreach ($entry in @(@('StoreLogo.png',50),@('Square44x44Logo.png',44),@('Square150x150Logo.png',150))) {
    $size = [int]$entry[1]
    $bitmap = New-Object Drawing.Bitmap($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([Drawing.ColorTranslator]::FromHtml('#17233A'))
    $pen = New-Object Drawing.Pen([Drawing.ColorTranslator]::FromHtml('#59D9BB'), [single]($size * 0.055))
    $graphics.DrawLine($pen,[single]($size*.18),[single]($size*.25),[single]($size*.18),[single]($size*.75))
    $graphics.DrawLine($pen,[single]($size*.18),[single]($size*.25),[single]($size*.33),[single]($size*.25))
    $graphics.DrawLine($pen,[single]($size*.18),[single]($size*.75),[single]($size*.33),[single]($size*.75))
    foreach ($y in @(.35,.50,.65)) { $graphics.DrawLine($pen,[single]($size*.43),[single]($size*$y),[single]($size*.80),[single]($size*$y)) }
    $bitmap.Save((Join-Path $Destination $entry[0]),[Drawing.Imaging.ImageFormat]::Png)
    if ($size -eq 44) {
        $handle = $bitmap.GetHicon()
        $icon = [Drawing.Icon]::FromHandle($handle)
        $stream = [IO.File]::Create((Join-Path $Destination 'app.ico'))
        try { $icon.Save($stream) } finally { $stream.Dispose(); $icon.Dispose() }
    }
    $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
