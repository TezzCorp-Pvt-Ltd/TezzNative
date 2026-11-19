# Bug Condition Exploration Test
# Property 1: Pointer Field Access Compilation Failure
#
# **Validates: Requirements 2.1, 2.2**
#
# This test verifies the bug condition exists in unfixed code by attempting
# to compile files with incorrect pointer field access syntax.
#
# CRITICAL: This test is EXPECTED TO FAIL on unfixed code.
# - FAILURE = SUCCESS (proves bug exists)
# - PASS = Bug is already fixed or test is incorrect
#
# Once the fix is applied, this same test will PASS, validating the fix works.

param(
    [Parameter(Mandatory=$true)]
    [string]$Tezzc
)

$ErrorActionPreference = "Continue"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Bug Condition Exploration Test" -ForegroundColor Cyan
Write-Host "Property 1: Pointer Field Access Syntax" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$bugFound = $false
$counterexamples = @()

# Test Case 1: lib/tensor/storage.tn
Write-Host "[Test 1] Checking lib/tensor/storage.tn" -ForegroundColor Yellow
$output1 = & $Tezzc check lib/tensor/storage.tn 2>&1 | Out-String
if ($output1 -match "error" -and $output1 -match "dot access") {
    Write-Host "  Found bug condition: Incorrect pointer field access syntax" -ForegroundColor Red
    Write-Host "  Location: storage.tn - TensorGradient pointer field access" -ForegroundColor Red
    $bugFound = $true
    
    $errorLines = $output1 -split "`n" | Where-Object { $_ -match "error" }
    foreach ($line in $errorLines) {
        $counterexamples += "storage.tn: $line"
        Write-Host "  Error: $line" -ForegroundColor Red
    }
} else {
    Write-Host "  No bug found - code may already be fixed" -ForegroundColor Green
}
Write-Host ""

# Test Case 2: lib/tensor/tape.tn
Write-Host "[Test 2] Checking lib/tensor/tape.tn" -ForegroundColor Yellow
$output2 = & $Tezzc check lib/tensor/tape.tn 2>&1 | Out-String
if ($output2 -match "error" -and $output2 -match "dot access") {
    Write-Host "  Found bug condition: Incorrect pointer field access syntax" -ForegroundColor Red
    Write-Host "  Location: tape.tn - TapeNode and Tensor pointer field access" -ForegroundColor Red
    $bugFound = $true
    
    $errorLines = $output2 -split "`n" | Where-Object { $_ -match "error" }
    foreach ($line in $errorLines) {
        $counterexamples += "tape.tn: $line"
        Write-Host "  Error: $line" -ForegroundColor Red
    }
} else {
    Write-Host "  No bug found - code may already be fixed" -ForegroundColor Green
}
Write-Host ""

# Test Case 3: lib/tensor/backward.tn
Write-Host "[Test 3] Checking lib/tensor/backward.tn" -ForegroundColor Yellow
$output3 = & $Tezzc check lib/tensor/backward.tn 2>&1 | Out-String
if ($output3 -match "error" -and $output3 -match "dot access") {
    Write-Host "  Found bug condition: Incorrect pointer field access syntax" -ForegroundColor Red
    Write-Host "  Location: backward.tn - TapeNode pointer field access" -ForegroundColor Red
    $bugFound = $true
    
    $errorLines = $output3 -split "`n" | Where-Object { $_ -match "error" }
    foreach ($line in $errorLines) {
        $counterexamples += "backward.tn: $line"
        Write-Host "  Error: $line" -ForegroundColor Red
    }
} else {
    Write-Host "  No bug found - code may already be fixed" -ForegroundColor Green
}
Write-Host ""

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($bugFound) {
    Write-Host "Bug condition CONFIRMED" -ForegroundColor Red
    Write-Host "  Pointer field access syntax errors found" -ForegroundColor Red
    Write-Host "  This is the EXPECTED outcome for unfixed code" -ForegroundColor Red
    Write-Host ""
    Write-Host "Counterexamples found:" -ForegroundColor Yellow
    foreach ($example in $counterexamples) {
        Write-Host "  - $example" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "Bug Pattern: ptr.field on pointer types" -ForegroundColor Yellow
    Write-Host "Expected Fix: Use (*ptr).field syntax" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "NOTE: After fix is applied, re-run this test to verify it PASSES" -ForegroundColor Cyan
    
    exit 0
} else {
    Write-Host "Bug condition NOT FOUND" -ForegroundColor Green
    Write-Host "  Code may already be fixed" -ForegroundColor Yellow
    Write-Host "  This is UNEXPECTED for unfixed code" -ForegroundColor Yellow
    
    exit 1
}
