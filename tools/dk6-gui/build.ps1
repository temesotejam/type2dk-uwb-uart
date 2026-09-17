param([string]$OutputDirectory = "$PSScriptRoot\out")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
$core = Join-Path $PSScriptRoot 'ProgrammerCore.cs'
$gui = Join-Path $PSScriptRoot 'MainForm.cs'
$app = Join-Path $OutputDirectory 'Type2DK-Programmer.exe'
& $compiler /nologo /target:winexe /platform:anycpu /optimize+ /warnaserror+ "/out:$app" "/win32manifest:$PSScriptRoot\app.manifest" /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.Xml.dll $core $gui
if ($LASTEXITCODE -ne 0) { throw 'GUI compilation failed' }
Copy-Item "$PSScriptRoot\Type2DK-Programmer.exe.config" $OutputDirectory
Copy-Item "$PSScriptRoot\README.md" "$OutputDirectory\README.md"
$tests = Join-Path $OutputDirectory 'tests'
New-Item -ItemType Directory -Force $tests | Out-Null
& $compiler /nologo /target:exe /warnaserror+ "/out:$tests\FakeProgrammer.exe" /reference:System.Core.dll "$PSScriptRoot\tests\FakeProgrammer.cs"
if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed' }
& $compiler /nologo /target:exe /warnaserror+ "/out:$tests\Tests.exe" /reference:System.Core.dll $core "$PSScriptRoot\tests\Tests.cs"
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
& "$tests\Tests.exe" "$tests\FakeProgrammer.exe"
if ($LASTEXITCODE -ne 0) { throw 'GUI integration tests failed' }
$preview = Join-Path $OutputDirectory 'preview.png'
$process = Start-Process -FilePath $app -ArgumentList @('--preview', ('"' + $preview + '"')) -Wait -PassThru
if ($process.ExitCode -ne 0 -or -not (Test-Path $preview)) { throw 'GUI startup/preview failed' }
$zip = Join-Path $OutputDirectory 'type2dk-programmer-gui-v1.0.0.zip'
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $app, "$app.config", "$OutputDirectory\README.md" -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path "$OutputDirectory\SHA256SUMS.txt" -Value "$hash  type2dk-programmer-gui-v1.0.0.zip" -Encoding ascii
@{version='1.0.0'; commit=$env:GITHUB_SHA; sha256=$hash; bytes=(Get-Item $zip).Length} | ConvertTo-Json | Set-Content -Path "$OutputDirectory\build-info.json" -Encoding utf8
Write-Host "GUI ZIP ready: $zip ($hash)"
