$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$wrapper = Join-Path $repoRoot 'android\gradlew.bat'

function Test-JdkHome([string] $jdkPath) {
    return [bool]($jdkPath -and (Test-Path -LiteralPath (Join-Path $jdkPath 'bin\java.exe')))
}

# Only this process. The user/system JAVA_HOME is left unchanged.
$jdkHome = $null
if (Test-JdkHome $env:JAVA_HOME) {
    $jdkHome = $env:JAVA_HOME
} elseif ($env:JAVA_HOME -match '[\\/]bin$' -and (Test-JdkHome (Split-Path $env:JAVA_HOME -Parent))) {
    $jdkHome = Split-Path $env:JAVA_HOME -Parent
} else {
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'Android\Android Studio\jbr')
    )) {
        if (Test-JdkHome $candidate) { $jdkHome = $candidate; break }
    }
}
if (-not $jdkHome) {
    throw 'Java 17 or newer is required. Install a JDK and ensure java is on PATH.'
}
$env:JAVA_HOME = $jdkHome
Write-Host "JAVA_HOME (esta compilacion): $env:JAVA_HOME"
if (-not (Test-Path -LiteralPath $wrapper)) {
    throw 'Gradle wrapper is missing from android\gradlew.bat.'
}
if (-not $env:ANDROID_HOME) {
    $sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    if (Test-Path -LiteralPath $sdk) { $env:ANDROID_HOME = $sdk }
}
if ($env:ANDROID_HOME) {
    $env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
}

Push-Location (Join-Path $repoRoot 'android')
try {
    # Gradle and javac print warnings on stderr; with 'Stop' PowerShell would abort on the first one.
    $ErrorActionPreference = 'Continue'
    & $wrapper assembleRelease
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed with exit code $LASTEXITCODE" }
    $apk = Join-Path $repoRoot 'android\app\build\outputs\apk\release\app-release.apk'
    Write-Host "APK: $apk"
} finally {
    Pop-Location
}
