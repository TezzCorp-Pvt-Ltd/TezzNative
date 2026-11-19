# Preservation Property Tests

## Overview

These tests verify that the pointer dereferencing fix preserves the intended runtime behavior of the tensor library. They are written **BEFORE** the fix is implemented but will be executed **AFTER** the fix is applied.

## Test Methodology

Following the observation-first methodology:

1. **Analyze unfixed code structure** - Identify non-buggy logic patterns
2. **Write property-based tests** - Capture intended behavior patterns
3. **Plan execution on fixed code** - Tests will run once code compiles
4. **Verify preservation** - Confirm fix maintains correct semantics

## Properties Tested

### Property 2.1: Reference Counting Preservation
**Validates: Requirement 3.2**

Tests that gradient retain/release operations correctly increment and decrement the `refs` field in `TensorGradient` structures.

- **Test File**: `test_preservation_refcount.tn`
- **Behavior**: Creates a gradient-tracked tensor, verifies gradient allocation
- **Expected**: Reference counting logic preserved after pointer dereferencing changes

### Property 2.2: Tape Recording Preservation
**Validates: Requirement 3.3**

Tests that autograd tape correctly captures operations with proper `op_type`, arguments, and gradient slots.

- **Test File**: `test_preservation_tape.tn`
- **Behavior**: Records tensor operations, verifies tape length
- **Expected**: Tape recording metadata preserved after pointer dereferencing changes

### Property 2.3: Backward Pass Preservation
**Validates: Requirement 3.4**

Tests that gradient computation logic is preserved for all operations (ADD, SUB, MUL, DIV, MATMUL, etc.).

- **Test File**: `test_preservation_backward.tn`
- **Behavior**: Computes gradients through operation chain, validates values
- **Expected**: Gradient computation correctness preserved after pointer dereferencing changes

### Property 2.4: Memory Management Preservation
**Validates: Requirement 3.5**

Tests that allocation, deallocation, and view operations maintain correct memory layouts.

- **Test File**: `test_preservation_memory.tn`
- **Behavior**: Creates tensors, views, clones; verifies memory properties
- **Expected**: Memory management correctness preserved after pointer dereferencing changes

### Property 2.5: Non-Pointer Struct Access Preservation
**Validates: Requirement 3.1**

Tests that non-pointer struct variables continue to use direct dot-access syntax.

- **Test File**: `test_preservation_struct.tn`
- **Behavior**: Accesses struct fields directly without dereferencing
- **Expected**: Non-pointer struct field access remains unchanged

### Property 2.6: Complex Operation Chains
**Validates: End-to-end behavior preservation**

Tests that chained operations maintain correct semantics throughout.

- **Test File**: `test_preservation_complex.tn`
- **Behavior**: Chains multiple operations (add, sub, mul), verifies results
- **Expected**: Complex operation semantics preserved

### Property 2.7: Matrix Operations
**Validates: Matrix multiplication and related operations**

Tests that matrix operations maintain correct dimensions and semantics.

- **Test File**: `test_preservation_matrix.tn`
- **Behavior**: Performs matrix multiply, verifies shape and gradients
- **Expected**: Matrix operation correctness preserved

## Usage

### PowerShell (Windows)
```powershell
.\tests\conformance\tensor_preservation_properties.ps1 -Tezzc C:\path\to\tezzc.exe
```

### Bash (Linux/macOS)
```bash
bash tests/conformance/tensor_preservation_properties.sh /path/to/tezzc
```

## Expected Outcomes

### Before Fix (Unfixed Code)
The preservation tests **cannot be run** because the tensor library does not compile due to pointer dereferencing errors.

### After Fix (Fixed Code)
The preservation tests should **ALL PASS**, confirming that:
- Reference counting logic is correct
- Tape recording captures operations properly
- Gradient computation produces correct values
- Memory management is sound
- Non-pointer struct access remains unchanged
- Complex operations work end-to-end
- Matrix operations maintain correctness

## Test Design Principles

1. **Observation-first**: Tests observe unfixed code structure to identify intended behavior
2. **Property-based**: Each test validates a behavioral property across multiple inputs
3. **Deterministic**: Tests produce consistent results for verification
4. **Focused**: Each test validates one specific aspect of preservation
5. **Comprehensive**: Tests cover all key behavioral aspects from requirements 3.1-3.5

## Integration with Bug Exploration

These preservation tests complement the bug exploration test:

- **Bug Exploration** (Task 1): Confirms the bug exists (pointer field access errors)
- **Preservation Tests** (Task 2): Confirms fix preserves behavior
- **Fix Implementation** (Task 3+): Applies pointer dereferencing corrections
- **Verification**: Both test suites pass after fix is applied

## Success Criteria

✅ All 7 preservation properties pass  
✅ No regression in non-tensor conformance tests  
✅ Bug exploration test transitions from "bug found" to "bug fixed"  
✅ All 26 tensor-dependent conformance tests pass  

## Property-Based Testing Approach

While TezzNative doesn't have a formal property testing framework, these tests implement property testing principles:

- **Multiple test cases**: Each property validates behavior across different inputs
- **Behavioral invariants**: Tests check that certain properties hold universally
- **Edge case coverage**: Tests include boundary conditions (empty tensors, size limits)
- **Compositional testing**: Complex operations built from simpler ones

This approach provides stronger guarantees than example-based testing alone.
