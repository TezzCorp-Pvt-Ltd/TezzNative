# Preservation Property Test Specification

## Purpose

These tests verify **Property 2: Preservation - Valid Struct Layout Preservation** from the type cycle detection bugfix design. They ensure that valid struct patterns continue working correctly after the fix is implemented.

## Test Files

### 1. valid_struct_pointer_cycle.tn
**Location**: `tests/conformance/typecheck/valid/valid_struct_pointer_cycle.tn`

**What It Tests**:
- Self-referential struct with pointer field: `struct Node { next:*Node }`
- Tree structure with two self-pointers: `struct Tree { left:*Tree, right:*Tree }`

**Why It Matters**:
Pointer cycles are VALID because pointer sizes are known and finite (typically 8 bytes on 64-bit systems). The fix must NOT break pointer-based recursion, which is a fundamental pattern in data structures (linked lists, trees, graphs).

**Validates Requirements**: 3.1 (Pointer fields SHALL CONTINUE TO be accepted)

---

### 2. valid_struct_forward_decl.tn
**Location**: `tests/conformance/typecheck/valid/valid_struct_forward_decl.tn`

**What It Tests**:
- Mutually recursive pointer references
- struct Node with `*List owner` field
- struct List with `*Node head` field

**Why It Matters**:
Mutually recursive pointers are a common pattern (e.g., a node points to its container, container points to nodes). This tests that the cycle detector correctly ignores pointer edges in the dependency graph.

**Validates Requirements**: 3.1, 3.2 (Struct definitions without value cycles SHALL CONTINUE TO work)

---

### 3. valid_struct_nested_acyclic.tn
**Location**: `tests/conformance/typecheck/valid/valid_struct_nested_acyclic.tn`

**What It Tests**:
- Acyclic value type dependencies: Point → Rect → Scene
- Nested struct values without forming a cycle
- Field access through multiple levels of nesting

**Why It Matters**:
This is the most critical preservation test. The fix defers layout computation to finalization, which must still correctly handle acyclic dependency chains. If this fails, it indicates the finalization logic is broken or produces different results than the original premature computation.

**Validates Requirements**: 3.2, 3.3, 3.5 (Acyclic dependencies SHALL CONTINUE TO produce correct layouts)

---

### 4. valid_struct_complex_layout.tn
**Location**: `tests/conformance/typecheck/valid/valid_struct_complex_layout.tn`

**What It Tests**:
- Mixed primitive types with different sizes (u8, i16, i32, i64)
- Multiple levels of nesting (Inner → Outer → Container)
- Mix of pointer and value fields
- Alignment and padding requirements

**Why It Matters**:
This tests that the layout computation logic (field offsets, struct sizes, alignment) produces identical results after deferring to finalization. If field offsets change, existing code could break at runtime even though it compiles.

**Validates Requirements**: 3.3 (Layout results SHALL CONTINUE TO be identical)

---

## Execution Instructions

### Phase 1: Baseline (Task 2) - COMPLETED
Run tests on UNFIXED code to establish baseline:
```powershell
tezzc check tests/conformance/typecheck/valid/valid_struct_pointer_cycle.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_forward_decl.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_nested_acyclic.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_complex_layout.tn
```

**Expected Result**: All 4 tests PASS (exit code 0, "OK" output)

### Phase 2: Verification (Task 3.3) - TO BE EXECUTED AFTER FIX
Run the SAME tests on FIXED code:
```powershell
# After rebuilding compiler with fix applied
tezzc check tests/conformance/typecheck/valid/valid_struct_pointer_cycle.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_forward_decl.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_nested_acyclic.tn
tezzc check tests/conformance/typecheck/valid/valid_struct_complex_layout.tn
```

**Expected Result**: All 4 tests STILL PASS (exit code 0, "OK" output)

**Failure Criteria**:
- If ANY test fails: The fix has introduced a regression
- If ANY test produces new warnings: Investigate potential issue
- If sizeof/field offsets change: The layout computation has changed (REGRESSION)

---

## Property Formal Specification

**Property 2: Preservation**

```
FOR ALL structDefinition WHERE NOT isBugCondition(structDefinition):
  // Bug condition is: struct dependency graph contains value type cycle
  
  ASSERT compile_unfixed(structDefinition).success == compile_fixed(structDefinition).success
  
  IF compile_fixed(structDefinition).success THEN:
    ASSERT layout_unfixed(structDefinition).size == layout_fixed(structDefinition).size
    ASSERT layout_unfixed(structDefinition).align == layout_fixed(structDefinition).align
    
    FOR ALL field IN structDefinition.fields:
      ASSERT field_offset_unfixed(field) == field_offset_fixed(field)
    END FOR
  END IF
END FOR
```

**In Plain English**:
For any struct that doesn't have a value type cycle, the fixed compiler must:
1. Compile it successfully if the unfixed compiler did
2. Produce the same struct size
3. Produce the same alignment
4. Produce the same field offsets

---

## Integration with Test Suite

These tests are part of the main conformance suite and will be automatically run by:
```powershell
.\tests\conformance\run.ps1 -Tezzc path\to\tezzc.exe
```

The test harness validates that all files in `tests/conformance/typecheck/valid/` compile successfully. Any file that fails to compile is reported as a test failure.

---

## Success Criteria

Task 2 is complete when:
- [x] All 4 test files created
- [x] All 4 tests PASS on unfixed code
- [x] Baseline behavior documented
- [x] Tests ready for Task 3.3 verification

Task 3.3 will be complete when:
- [ ] All 4 tests PASS on fixed code
- [ ] No regressions detected
- [ ] Layout results verified as identical
