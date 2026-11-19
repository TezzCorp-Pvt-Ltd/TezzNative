param(
  [Parameter(Mandatory=$true)]
  [string]$Tezzc
)

$ErrorActionPreference = "Stop"

$TezzcPath = Resolve-Path $Tezzc -ErrorAction Stop

Write-Host "=================================================" -ForegroundColor Cyan
Write-Host "  Preservation Property Tests - Task 2" -ForegroundColor Cyan
Write-Host "=================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Compiler: $TezzcPath" -ForegroundColor Gray
Write-Host ""

$TestFiles = @(
  "tests\conformance\typecheck\valid\valid_struct_pointer_cycle.tn",
  "tests\conformance\typecheck\valid\valid_struct_forward_decl.tn",
  "tests\conformance\typecheck\valid\valid_struct_nested_acyclic.tn",
  "tests\conformance\typecheck\valid\valid_struct_complex_layout.tn"
)

$TestDescriptions = @{
  "valid_struct_pointer_cycle.tn" = "Pointer cycles (self-referential structs)";
  "valid_struct_forward_decl.tn" = "Mutually recursive pointers";
  "valid_struct_nested_acyclic.tn" = "Acyclic value dependencies";
  "valid_struct_complex_layout.tn" = "Complex mixed layouts"
}

$TotalTests = $TestFiles.Count
$PassedTests = 0
$FailedTests = 0
$FailedFiles = @()

foreach ($TestFile in $TestFiles) {
  $FileName = Split-Path $TestFile -Leaf
  $Description = $TestDescriptions[$FileName]
  
  Write-Host "Testing: $FileName" -ForegroundColor Yellow
  Write-Host "  $Description" -ForegroundColor Gray
  
  $Output = & $TezzcPath check $TestFile 2>&1
  $ExitCode = $LASTEXITCODE
  
  if ($ExitCode -eq 0) {
    Write-Host "  PASS" -ForegroundColor Green
    $PassedTests++
  } else {
    Write-Host "  FAIL (exit code: $ExitCode)" -ForegroundColor Red
    $Output | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
    $FailedTests++
    $FailedFiles += $FileName
  }
  
  Write-Host ""
}

Write-Host "=================================================" -ForegroundColor Cyan
Write-Host "  SUMMARY" -ForegroundColor Cyan
Write-Host "=================================================" -ForegroundColor Cyan
Write-Host "Total Tests:  $TotalTests"
Write-Host "Passed:       $PassedTests" -ForegroundColor Green
Write-Host "Failed:       $FailedTests" -ForegroundColor $(if ($FailedTests -eq 0) { "Green" } else { "Red" })
Write-Host ""

if ($FailedTests -eq 0) {
  Write-Host "All preservation tests PASSED" -ForegroundColor Green
  exit 0
} else {
  Write-Host "Preservation tests FAILED" -ForegroundColor Red
  Write-Host "Failed tests:" -ForegroundColor Red
  foreach ($File in $FailedFiles) {
    Write-Host "  - $File" -ForegroundColor Red
  }
  exit 1
}
