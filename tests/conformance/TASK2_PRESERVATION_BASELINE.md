# Task 2: Preservation Property Tests - Baseline Behavior

## Overview

This document captures the baseline behavior of the TezzNative compiler on UNFIXED code for valid struct patterns. These tests establish the expected behavior that MUST be preserved after implementing the type cycle detection fix.

**Test Date**: Executed on unfixed compiler at `C:\GitHub\rohit\TezzNative\tezzc.exe`

## Test Results on UNFIXED Code

### Test A: Pointer Cycles (Self-Referential Structs)

**File**: `tests/conformance/typecheck/valid/valid_struct_pointer_cycle.tn`

**Pattern Tested**:
- struct Node with `*Node next` field (self-referential pointer)
- struct Tree with `*Tree left` and `*Tree right` fields

**Result**: ✅ **PASS** - Compiles successfully with exit code 0

**Baseline Behavior**:
- Compiler accepts pointer-based recursive references
- Type checking succeeds for self-referential pointer fields
- No errors or warnings emitted

**Expected After Fix**: This pattern MUST continue to compile successfully without changes.

---

### Test B: Mutually Recursive Pointers

**File**: `tests/conformance/typecheck/valid/valid_struct_forward_decl.tn`

**Pattern Tested**:
- struct Node containing `*List owner` field
- struct List containing `*Node head` field
- Mutually recursive pointer references

**Result**: ✅ **PASS** - Compiles successfully with exit code 0

**Baseline Behavior**:
- Compiler accepts mutually recursive pointer references
- Type checking resolves cross-references correctly
- No errors or warnings emitted

**Expected After Fix**: This pattern MUST continue to compile successfully without changes.

---

### Test C: Acyclic Value Dependencies

**File**: `tests/conformance/typecheck/valid/valid_struct_nested_acyclic.tn`

**Pattern Tested**:
- Point → Rect → Scene dependency chain (no cycles)
- Nested value types without circular dependencies

**Result**: ✅ **PASS** - Compiles successfully with exit code 0

**Baseline Behavior**:
- Compiler accepts acyclic nested struct definitions
- Layout computation succeeds for dependency chains
- Field access works correctly for deeply nested fields
- No errors or warnings emitted

**Expected After Fix**: This pattern MUST continue to compile successfully with identical layout results.

---

### Test D: Complex Mixed Layouts

**File**: `tests/conformance/typecheck/valid/valid_struct_complex_layout.tn`

**Pattern Tested**:
- Mixed primitive types (u8, i64, u16, i32)
- Nested structs with value fields
- Pointer fields mixed with value fields
- Multiple levels of nesting (Inner → Outer → Container)

**Result**: ✅ **PASS** - Compiles successfully with exit code 0

**Baseline Behavior**:
- Compiler handles complex nested layouts correctly
- Field access works for deeply nested fields
- Mixed pointer and value fields coexist properly
- No errors or warnings emitted

**Expected After Fix**: This pattern MUST continue to compile successfully with identical field offsets and sizes.

---

## Summary

**Total Tests**: 4
**Passed on Unfixed Code**: 4 (100%)
**Failed on Unfixed Code**: 0

All preservation tests establish baseline behavior for valid struct patterns that do NOT contain value type cycles. These patterns use:
- Pointer-based recursion (valid, pointers have known size)
- Acyclic value dependencies (valid, no circular size constraints)
- Complex but legal struct layouts

## Verification Requirements

After implementing the type cycle detection fix (Task 3), these same four tests MUST:
1. Continue to compile successfully (exit code 0)
2. Produce "OK" output from `tezzc check`
3. Show no new errors or warnings
4. Produce identical struct layouts (sizes, alignments, field offsets)

Any regression in these tests indicates the fix has broken valid code and must be corrected before the fix can be considered complete.

## Property Specification

**Property 2: Preservation - Valid Struct Layout Preservation**

_For any_ struct definition that does NOT contain a value type cycle (including forward declarations, pointer cycles, and acyclic dependencies), the fixed compiler SHALL produce exactly the same struct layout (size, alignment, field offsets) as the original compiler, and the program SHALL compile successfully without new errors.

**Validates Requirements**: 3.1, 3.2, 3.3, 3.4, 3.5 from bugfix.md
