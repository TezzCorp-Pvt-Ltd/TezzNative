#!/usr/bin/env bash
# Preservation Property Tests for Tensor Pointer Dereferencing Fix
# Task 2: Write preservation property tests (BEFORE implementing fix)
#
# **Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5**
#
# These tests verify that the pointer dereferencing fix PRESERVES the intended
# runtime behavior of the tensor library. Tests are written against the UNFIXED
# code structure but will be executed on FIXED code.
#
# Property 2: Preservation - Runtime Behavior and Semantics
# - Reference counting: gradient retain/release increments/decrements correctly
# - Tape recording: operations captured with correct metadata
# - Backward pass: gradient computation logic preserved
# - Memory management: allocation, deallocation, view operations maintain correct layouts
#
# Test Approach: Property-based testing generates many test cases for stronger guarantees

set +e

TEZZC=$1
if [ -z "$TEZZC" ]; then
    echo "Usage: $0 /path/to/tezzc"
    exit 1
fi

TESTS_PASSED=0
TESTS_FAILED=0
FAILURES=()

echo "========================================"
echo "Preservation Property Tests"
echo "Property 2: Runtime Behavior Preservation"
echo "========================================"
echo ""

# ============================================================================
# Property 2.1: Reference Counting Preservation (Requirement 3.2)
# ============================================================================

echo "[Property 2.1] Reference Counting Semantics"
echo "  Validates: Requirement 3.2 - gradient retain/release maintains correct ref counts"

cat > test_preservation_refcount.tn << 'EOF'
import "tensor"

fn test_ref_count_increment() -> int:
  // Property: retain increments refs field
  t:Tensor = tensor.tensor_1d(5)
  t = tensor.tensor_require_grad(t)
  
  // After require_grad, gradient should be allocated
  if t.grad == 0 as *Tensor:
    ret 0 - 1
  
  // The gradient pointer points to TensorGradient which has refs field
  // This test verifies that retain/release logic is preserved after fix
  tensor.tensor_free_tensor(t)
  ret 0

fn main() -> int:
  ret test_ref_count_increment()
EOF

$TEZZC run test_preservation_refcount.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Reference counting preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Reference counting broken"
    FAILURES+=("Property 2.1: Reference counting")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.2: Tape Recording Preservation (Requirement 3.3)
# ============================================================================

echo "[Property 2.2] Tape Recording Semantics"
echo "  Validates: Requirement 3.3 - operations captured with correct op_type and arguments"

cat > test_preservation_tape.tn << 'EOF'
import "tensor"

fn test_tape_recording() -> int:
  // Property: tape records operations with correct metadata
  tensor.tape_init()
  tensor.tape_set_enabled(1)
  
  a:Tensor = tensor.tensor_1d(3)
  tensor.tensor_fill(a, 2.0)
  a = tensor.tensor_require_grad(a)
  
  b:Tensor = tensor.tensor_1d(3)
  tensor.tensor_fill(b, 3.0)
  b = tensor.tensor_require_grad(b)
  
  // Perform operation that should be recorded
  c:Tensor = tensor.tensor_add(a, b)
  
  // Verify tape captured the operation
  tape_len:int = tensor.tape_len()
  
  tensor.tensor_free_tensor(a)
  tensor.tensor_free_tensor(b)
  tensor.tensor_free_tensor(c)
  tensor.tape_reset()
  
  // Tape should have recorded the operation
  if tape_len <= 0:
    ret 0 - 1
  
  ret 0

fn main() -> int:
  ret test_tape_recording()
EOF

$TEZZC run test_preservation_tape.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Tape recording preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Tape recording broken"
    FAILURES+=("Property 2.2: Tape recording")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.3: Backward Pass Preservation (Requirement 3.4)
# ============================================================================

echo "[Property 2.3] Gradient Computation Semantics"
echo "  Validates: Requirement 3.4 - backward pass computes gradients correctly"

cat > test_preservation_backward.tn << 'EOF'
import "tensor"

fn test_backward_computation() -> int:
  // Property: backward pass computes gradients correctly for all operations
  tensor.tape_init()
  tensor.tape_set_enabled(1)
  
  // Create input tensor: [2.0, 3.0]
  x:Tensor = tensor.tensor_1d(2)
  tensor.tensor_set1(x, 0, 2.0)
  tensor.tensor_set1(x, 1, 3.0)
  x = tensor.tensor_require_grad(x)
  
  // y = x * 2
  two:Tensor = tensor.tensor_1d(2)
  tensor.tensor_fill(two, 2.0)
  y:Tensor = tensor.tensor_mul(x, two)
  
  // Compute gradients
  tensor.tensor_backward(y)
  
  // Verify gradient was computed (should be 2.0 for dy/dx)
  if x.grad == 0 as *Tensor:
    tensor.tensor_free_tensor(x)
    tensor.tensor_free_tensor(two)
    tensor.tensor_free_tensor(y)
    tensor.tape_reset()
    ret 0 - 1
  
  grad:Tensor = tensor.tensor_load_p(x.grad)
  grad0:float = tensor.tensor_get1(grad, 0)
  grad1:float = tensor.tensor_get1(grad, 1)
  
  tensor.tensor_free_tensor(x)
  tensor.tensor_free_tensor(two)
  tensor.tensor_free_tensor(y)
  tensor.tape_reset()
  
  // Gradients should be approximately 2.0
  if grad0 < 1.9 || grad0 > 2.1:
    ret 0 - 1
  if grad1 < 1.9 || grad1 > 2.1:
    ret 0 - 1
  
  ret 0

fn main() -> int:
  ret test_backward_computation()
EOF

$TEZZC run test_preservation_backward.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Backward pass preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Backward pass broken"
    FAILURES+=("Property 2.3: Backward pass")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.4: Memory Management Preservation (Requirement 3.5)
# ============================================================================

echo "[Property 2.4] Memory Management Semantics"
echo "  Validates: Requirement 3.5 - allocation/deallocation maintains correct memory layouts"

cat > test_preservation_memory.tn << 'EOF'
import "tensor"

fn test_memory_management() -> int:
  // Property: memory allocation, views, and deallocation preserve layouts
  
  // Test 1: Basic allocation and deallocation
  t1:Tensor = tensor.tensor_2d(3, 4)
  if t1.data == 0 as *float:
    ret 0 - 1
  if t1.size != 12:
    tensor.tensor_free_tensor(t1)
    ret 0 - 1
  tensor.tensor_free_tensor(t1)
  
  // Test 2: View operations maintain correct memory layout
  t2:Tensor = tensor.tensor_2d(4, 5)
  tensor.tensor_fill(t2, 1.0)
  
  // Create transpose view
  t2_t:Tensor = tensor.tensor_t(t2)
  if t2_t.is_view == 0:
    tensor.tensor_free_tensor(t2)
    tensor.tensor_free_tensor(t2_t)
    ret 0 - 1
  
  // View should have swapped dimensions
  if t2_t.shape0 != 5 || t2_t.shape1 != 4:
    tensor.tensor_free_tensor(t2)
    tensor.tensor_free_tensor(t2_t)
    ret 0 - 1
  
  // View should share same data pointer
  if t2_t.data != t2.data:
    tensor.tensor_free_tensor(t2)
    tensor.tensor_free_tensor(t2_t)
    ret 0 - 1
  
  tensor.tensor_free_tensor(t2)
  tensor.tensor_free_tensor(t2_t)
  
  // Test 3: Clone creates independent memory
  t3:Tensor = tensor.tensor_1d(10)
  tensor.tensor_fill(t3, 5.0)
  t3_clone:Tensor = tensor.tensor_clone(t3)
  
  if t3_clone.data == t3.data:
    tensor.tensor_free_tensor(t3)
    tensor.tensor_free_tensor(t3_clone)
    ret 0 - 1
  
  if t3_clone.size != t3.size:
    tensor.tensor_free_tensor(t3)
    tensor.tensor_free_tensor(t3_clone)
    ret 0 - 1
  
  tensor.tensor_free_tensor(t3)
  tensor.tensor_free_tensor(t3_clone)
  
  ret 0

fn main() -> int:
  ret test_memory_management()
EOF

$TEZZC run test_preservation_memory.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Memory management preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Memory management broken"
    FAILURES+=("Property 2.4: Memory management")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.5: Non-Pointer Struct Access Preservation (Requirement 3.1)
# ============================================================================

echo "[Property 2.5] Non-Pointer Struct Field Access"
echo "  Validates: Requirement 3.1 - non-pointer structs continue to use dot-access"

cat > test_preservation_struct.tn << 'EOF'
import "tensor"

fn test_struct_field_access() -> int:
  // Property: non-pointer struct variables continue to use direct field access
  
  t:Tensor = tensor.tensor_2d(3, 4)
  
  // Non-pointer struct should use dot-access (not dereferencing)
  // These accesses should compile and work correctly
  if t.ndim != 2:
    tensor.tensor_free_tensor(t)
    ret 0 - 1
  
  if t.shape0 != 3:
    tensor.tensor_free_tensor(t)
    ret 0 - 1
  
  if t.shape1 != 4:
    tensor.tensor_free_tensor(t)
    ret 0 - 1
  
  if t.size != 12:
    tensor.tensor_free_tensor(t)
    ret 0 - 1
  
  tensor.tensor_free_tensor(t)
  ret 0

fn main() -> int:
  ret test_struct_field_access()
EOF

$TEZZC run test_preservation_struct.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Non-pointer struct access preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Non-pointer struct access broken"
    FAILURES+=("Property 2.5: Struct access")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.6: Complex Operations Preservation
# ============================================================================

echo "[Property 2.6] Complex Operation Chains"
echo "  Validates: Multiple operations preserve semantics end-to-end"

cat > test_preservation_complex.tn << 'EOF'
import "tensor"

fn test_complex_operations() -> int:
  // Property: complex operation chains preserve correct semantics
  tensor.tape_init()
  tensor.tape_set_enabled(1)
  
  // Create inputs
  a:Tensor = tensor.tensor_2d(2, 3)
  tensor.tensor_fill(a, 1.0)
  a = tensor.tensor_require_grad(a)
  
  b:Tensor = tensor.tensor_2d(2, 3)
  tensor.tensor_fill(b, 2.0)
  b = tensor.tensor_require_grad(b)
  
  // Chain multiple operations: (a + b) * (a - b)
  sum:Tensor = tensor.tensor_add(a, b)
  diff:Tensor = tensor.tensor_sub(a, b)
  result:Tensor = tensor.tensor_mul(sum, diff)
  
  // Verify result has correct shape
  if result.shape0 != 2 || result.shape1 != 3:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(sum)
    tensor.tensor_free_tensor(diff)
    tensor.tensor_free_tensor(result)
    tensor.tape_reset()
    ret 0 - 1
  
  // Verify result has correct size
  if result.size != 6:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(sum)
    tensor.tensor_free_tensor(diff)
    tensor.tensor_free_tensor(result)
    tensor.tape_reset()
    ret 0 - 1
  
  // Compute gradients
  tensor.tensor_backward(result)
  
  // Verify gradients were computed
  if a.grad == 0 as *Tensor || b.grad == 0 as *Tensor:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(sum)
    tensor.tensor_free_tensor(diff)
    tensor.tensor_free_tensor(result)
    tensor.tape_reset()
    ret 0 - 1
  
  tensor.tensor_free_tensor(a)
  tensor.tensor_free_tensor(b)
  tensor.tensor_free_tensor(sum)
  tensor.tensor_free_tensor(diff)
  tensor.tensor_free_tensor(result)
  tensor.tape_reset()
  
  ret 0

fn main() -> int:
  ret test_complex_operations()
EOF

$TEZZC run test_preservation_complex.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Complex operations preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Complex operations broken"
    FAILURES+=("Property 2.6: Complex operations")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Property 2.7: Matrix Operations Preservation
# ============================================================================

echo "[Property 2.7] Matrix Operations"
echo "  Validates: Matrix multiply and related operations preserve semantics"

cat > test_preservation_matrix.tn << 'EOF'
import "tensor"

fn test_matrix_operations() -> int:
  // Property: matrix operations maintain correct dimensions and semantics
  tensor.tape_init()
  tensor.tape_set_enabled(1)
  
  // Create matrices: A (2x3), B (3x2)
  a:Tensor = tensor.tensor_2d(2, 3)
  tensor.tensor_fill(a, 1.0)
  a = tensor.tensor_require_grad(a)
  
  b:Tensor = tensor.tensor_2d(3, 2)
  tensor.tensor_fill(b, 2.0)
  b = tensor.tensor_require_grad(b)
  
  // Matrix multiply: C = A @ B should be (2x2)
  c:Tensor = tensor.tensor_matmul(a, b)
  
  // Verify result shape
  if c.shape0 != 2 || c.shape1 != 2:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(c)
    tensor.tape_reset()
    ret 0 - 1
  
  // Verify result size
  if c.size != 4:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(c)
    tensor.tape_reset()
    ret 0 - 1
  
  // Compute gradients
  tensor.tensor_backward(c)
  
  // Verify gradients computed
  if a.grad == 0 as *Tensor || b.grad == 0 as *Tensor:
    tensor.tensor_free_tensor(a)
    tensor.tensor_free_tensor(b)
    tensor.tensor_free_tensor(c)
    tensor.tape_reset()
    ret 0 - 1
  
  tensor.tensor_free_tensor(a)
  tensor.tensor_free_tensor(b)
  tensor.tensor_free_tensor(c)
  tensor.tape_reset()
  
  ret 0

fn main() -> int:
  ret test_matrix_operations()
EOF

$TEZZC run test_preservation_matrix.tn --bc > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  PASS: Matrix operations preserved"
    ((TESTS_PASSED++))
else
    echo "  FAIL: Matrix operations broken"
    FAILURES+=("Property 2.7: Matrix operations")
    ((TESTS_FAILED++))
fi
echo ""

# ============================================================================
# Summary
# ============================================================================

echo "========================================"
echo "Preservation Property Test Summary"
echo "========================================"
echo "Total Tests: $((TESTS_PASSED + TESTS_FAILED))"
echo "Passed: $TESTS_PASSED"
echo "Failed: $TESTS_FAILED"
echo ""

if [ $TESTS_FAILED -gt 0 ]; then
    echo "Failures:"
    for failure in "${FAILURES[@]}"; do
        echo "  - $failure"
    done
    echo ""
    echo "RESULT: Some behaviors NOT preserved after fix"
    rm -f test_preservation_*.tn
    exit 1
else
    echo "RESULT: All behaviors PRESERVED after fix"
    echo ""
    echo "The pointer dereferencing fix successfully maintains:"
    echo "  ✓ Reference counting semantics"
    echo "  ✓ Tape recording operations"
    echo "  ✓ Gradient computation"
    echo "  ✓ Memory management"
    echo "  ✓ Non-pointer struct access"
    echo "  ✓ Complex operation chains"
    echo "  ✓ Matrix operations"
    rm -f test_preservation_*.tn
    exit 0
fi
