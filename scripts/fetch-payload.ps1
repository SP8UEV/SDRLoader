<#
.SYNOPSIS
  Builds the offline package's payload\ folder: the files that may be
  redistributed, each checked, each with the exact source code its licence asks for.

.DESCRIPTION
  The list comes from the program itself (sdrloader-cli.exe
  --list-payload), so there is one catalogue and not two. Each file is fetched
  from its publisher's own address and checked -- size and SHA-256, or the
  Authenticode signer -- exactly as the program checks it.

  Packages marked "needs-source-review" are left out unless
  -IncludeNeedsSourceReview is given. Read THIRD-PARTY.md before you use it:
  shipping a GPL binary means shipping its complete corresponding source.

  Manufacturers' installers (SDRplay, Nuand, Analog Devices, Microsoft) are
  never put in the payload; the program always fetches them from the publisher.

.EXAMPLE
  ./scripts/fetch-payload.ps1 -Cli build/sdrloader-cli.exe -Out dist/payload
#>
param(
    [string]$Cli = (Join-Path $PSScriptRoot '..\build\sdrloader-cli.exe'),
    [string]$Out = (Join-Path $PSScriptRoot '..\dist\payload'),
    [switch]$IncludeNeedsSourceReview
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not (Test-Path $Cli)) { throw "Build the program first: $Cli was not found." }
New-Item -ItemType Directory -Force -Path $Out, (Join-Path $Out 'sources') | Out-Null

$included = @()
foreach ($line in (& $Cli --list-payload)) {
    if (-not $line.Trim()) { continue }
    $id, $file, $url, $size, $sha, $signer, $policy, $source = $line -split "`t"

    if ($policy -eq 'needs-source-review' -and -not $IncludeNeedsSourceReview) {
        Write-Host "skip   ${id}: its source needs review first (see THIRD-PARTY.md)"
        continue
    }

    $dest = Join-Path $Out $file
    $part = "$dest.part"
    Write-Host "fetch  ${id}: $url"
    Invoke-WebRequest -Uri $url -OutFile $part -UseBasicParsing
    Move-Item -Force $part $dest

    $length = (Get-Item $dest).Length
    if ([int64]$size -gt 0 -and $length -ne [int64]$size) {
        Remove-Item $dest
        throw "${id}: $length bytes, the publisher's file has $size."
    }
    if ($sha) {
        $hash = (Get-FileHash -Algorithm SHA256 $dest).Hash.ToLowerInvariant()
        if ($hash -ne $sha) {
            Remove-Item $dest
            throw "${id}: SHA-256 $hash is not the published $sha."
        }
    }
    if ($signer) {
        $sig = Get-AuthenticodeSignature -FilePath $dest
        if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notlike "*$signer*") {
            Remove-Item $dest
            throw "${id}: signature '$($sig.Status)' by '$($sig.SignerCertificate.Subject)', expected '$signer'."
        }
    }
    Write-Host "       checked"

    if ($source) {
        $name = "$id-source-" + ($source -split '/')[-1]
        Write-Host "source ${id}: $source"
        Invoke-WebRequest -Uri $source -OutFile (Join-Path $Out "sources\$name") -UseBasicParsing
    }
    $included += "$file  ($id; source: sources\$id-source-*)"
}

@"
SDRLoader -- offline package
=======================================

The program uses the files in this folder instead of downloading them, after
checking each one exactly as it checks a download (size and SHA-256, or the
publisher's digital signature).

Files:
  $($included -join "`r`n  ")

Every file here is open-source software under its own licence (see
THIRD-PARTY.md). The complete source code of each is in sources\, as the
GPL and LGPL require when their binaries are passed on.

Manufacturers' installers are not in this folder: the program fetches them
from the manufacturer, and they show their own licence terms.
"@ | Set-Content -Encoding UTF8 -Path (Join-Path $Out 'README.txt')

Write-Host "payload ready: $Out"
