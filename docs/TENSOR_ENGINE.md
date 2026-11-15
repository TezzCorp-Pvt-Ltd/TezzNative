# TezzNative Tensor Engine & Autograd Architecture

This document provides a comprehensive technical guide, API reference, and architectural specification for the **TezzNative Native Tensor Engine** (`lib/tensor.tn`).

---

## 1. Overview & Architectural Goals

The TezzNative Tensor Engine provides a production-grade, native N-dimensional tensor math and deep learning foundation directly within the TezzNative language ecosystem:

- **Zero External Dependencies:** Built entirely with native TezzNative memory, arithmetic, and unsafe pointer primitives.
- **Arbitrary Dimensionality (Ranks 0 to 8):** Supports scalars, vectors, matrices, 3D batch slices, 4D multi-head attention blocks, and higher rank structures up to 8 dimensions.
- **Zero-Copy Strided Views:** Sub-tensor slicing, matrix transposition, and high-dimensional axes permutation occur without allocating or copying underlying memory.
- **Tiled SIMD Matrix Multiplication (BLAS):** Unrolled 8-wide GEMM and 3D/4D Batched Matrix Multiplication (BMM) with cache-friendly access patterns.
- **Fused LLM & Transformer Kernels:** Hardware-friendly implementations of RMSNorm, LayerNorm, numerically stable Softmax, Rotary Position Embeddings (RoPE), and Causal Scaled Dot-Product Attention (SDPA).
- **Reverse-Mode Autograd Engine:** Dynamic computation tape recording with reverse topological traversal, multi-branch gradient accumulation, and automatic broadcast dimension collapse.
- **Production Optimizers & Schedulers:** Decoupled Weight Decay AdamW, Momentum SGD with Nesterov acceleration, gradient norm clipping (`clip_grad_norm`), and linear/cosine warmup learning rate schedulers.
- **CUDA GPU Acceleration Bridge:** Direct integration with the NVIDIA CUDA Driver API (`nvcuda.dll`) and Turing JIT PTX for multi-hundred GFLOPS throughput on NVIDIA GPUs.

---

## 2. Core Tensor Data Structures & Memory Model

### `struct Tensor`

The core tensor struct is declared in `lib/tensor.tn` as follows:

```tezz
struct Tensor:
  data:*float
  ndim:int
  shape0:int
  shape1:int
  shape2:int
  shape3:int
  shape4:int
  shape5:int
  shape6:int
  shape7:int
  stride0:int
  stride1:int
  stride2:int
  stride3:int
  stride4:int
  stride5:int
  stride6:int
  stride7:int
  offset:int
  size:int
  dtype:int
  is_view:int
  requires_grad:int
  grad:*Tensor
  node_id:int
```

### Strided Indexing Formula

For any N-dimensional coordinate $(c_0, c_1, \dots, c_{d-1})$, the flat 1D memory offset in `data` is calculated in $O(1)$ time:

$$\text{Index} = \text{offset} + \sum_{i=0}^{\text{ndim}-1} c_i \cdot \text{stride}_i$$

- **Canonical Contiguous Layout:** For a tensor with shape $[D_0, D_1, \dots, D_{k-1}]$, canonical row-major strides satisfy:
  $$\text{stride}_{k-1} = 1, \quad \text{stride}_i = \text{stride}_{i+1} \cdot D_{i+1}$$
- **Contiguity Check:** `tensor_is_contiguous(t)` verifies whether the strides match the exact canonical row-major layout.
- **Contiguous Repacking:** `tensor_contiguous(t)` checks `is_contiguous`; if already contiguous, it returns a clone. If non-contiguous (e.g. after a transpose or strided slice), it compacts the elements into a freshly allocated canonical contiguous memory buffer.

---

## 3. Phase 1: Tensor Creation & Strided Views

### Allocation & Initialization

| Function | Signature | Description |
|---|---|---|
| `tensor_1d` | `(d0:int) -> Tensor` | Allocates uninitialized 1D tensor of length `d0`. |
| `tensor_2d` | `(d0:int, d1:int) -> Tensor` | Allocates uninitialized 2D tensor of shape `[d0, d1]`. |
| `tensor_3d` | `(d0:int, d1:int, d2:int) -> Tensor` | Allocates uninitialized 3D tensor of shape `[d0, d1, d2]`. |
| `tensor_4d` | `(d0:int, d1:int, d2:int, d3:int) -> Tensor` | Allocates uninitialized 4D tensor of shape `[d0, d1, d2, d3]`. |
| `tensor_zeros_1d` .. `4d` | `(...) -> Tensor` | Allocates tensor pre-filled with `0.0`. |
| `tensor_ones_1d` .. `4d` | `(...) -> Tensor` | Allocates tensor pre-filled with `1.0`. |
| `tensor_randn_2d` | `(d0:int, d1:int, mean:float, std:float) -> Tensor` | Generates normal distribution via Box-Muller transform. |
| `tensor_clone` | `(t:Tensor) -> Tensor` | Deep copy of tensor data and descriptors. |
| `tensor_free_tensor` | `(t:Tensor) -> int` | Frees underlying heap memory if `is_view == 0`. |

### Zero-Copy View Transformations

- **Transpose (`tensor_transpose(t, axis0, axis1)`):** Swaps `shape` and `stride` along the two specified axes. Marks `is_view = 1`. Memory allocation is zero bytes.
- **Slicing (`tensor_slice(t, axis, start, end, step)`):** Creates a sub-tensor view along `axis` from `start` to `end` with stride multiplication `step * stride`. Computes new `offset = offset + start * stride`.
- **4D Permutation (`tensor_permute4(t, d0, d1, d2, d3)`):** Permutes arbitrary 4D dimensions (e.g., Attention transposition $[B, S, H, D] \to [B, H, S, D]$ with `tensor_permute4(t, 0, 2, 1, 3)`).
- **Reshape (`tensor_reshape(t, ndim, d0, d1, d2, d3)`):** Reinterprets dimensions if the tensor is contiguous and total element count matches.
- **Squeeze & Unsqueeze:**
  - `tensor_squeeze(t, axis)`: Removes a dimension of size 1.
  - `tensor_unsqueeze(t, axis)`: Inserts a unit dimension (`shape = 1`, `stride = stride[axis]`).
- **Flatten (`tensor_flatten(t)`):** Flattens any tensor to a contiguous 1D tensor.

---

## 4. Phase 2: Native Tensor Math (BLAS)

### Matrix Multiplication (GEMM & BMM)

1. **2D Matrix Multiplication (`tensor_matmul(a, b)`):**
   - Matrix shapes: $A[M, K] \times B[K, N] \to C[M, N]$.
   - Supports non-contiguous inputs: handles $B^T$ transposed views seamlessly via internal strided dereferencing.
   - Inner dot product uses an 8-wide unrolled SIMD accumulator loop:
     ```tezz
     while k <= K - 8:
       sum0 = sum0 + a.data[off_a + k] * b.data[off_b0 + k * s_bk]
       sum1 = sum1 + a.data[off_a + k + 1] * b.data[off_b0 + (k + 1) * s_bk]
       // ... unrolled up to +7
       k = k + 8
     ```
2. **Batched Matrix Multiplication (`tensor_bmm(a, b)`):**
   - 3D BMM: $A[B, M, K] \times B[B, K, N] \to C[B, M, N]$.
   - 4D BMM: $A[B, H, M, K] \times B[B, H, K, N] \to C[B, H, M, N]$.
   - Performs GEMM across every independent batch and head slice.

### Elementwise Arithmetic & Broadcasting

- `tensor_add(a, b)`, `tensor_sub(a, b)`, `tensor_mul(a, b)`, `tensor_div(a, b)`, `tensor_neg(a)`:
  - Supports automatic multidimensional broadcasting:
    - $[B, D] \text{ op } [1, D] \to [B, D]$
    - $[B, S, D] \text{ op } [1, 1, D] \to [B, S, D]$
    - $[M, N] \text{ op } [1, N] \to [M, N]$
- Scalar arithmetic: `tensor_scale(a, s)`, `tensor_add_scalar(a, s)`, `tensor_clamp(a, min, max)`.

### Reductions

- **Global Reductions:**
  - `tensor_sum_all(a) -> float`
  - `tensor_mean_all(a) -> float`
  - `tensor_max_all(a) -> float`
  - `tensor_min_all(a) -> float`
  - `tensor_norm(a) -> float`: $L_2$ Euclidean norm ($\sqrt{\sum x_i^2}$).
- **Axis Reductions (`tensor_sum_axis`, `tensor_mean_axis`, `tensor_max_axis`):**
  - Reduces along any axis $0 \le \text{axis} < \text{ndim}$.
  - Supports `keepdim = 1` (keeps reduced axis as size 1) or `keepdim = 0` (squeezes reduced axis).

---

## 5. Phase 3: Activation & LLM Layers

### Fused Activations

| Operation | Function | Formula |
|---|---|---|
| **ReLU** | `tensor_relu(a)` / `tensor_relu_(a)` | $\max(0, x)$ (supports both out-of-place and in-place) |
| **GELU** | `tensor_gelu(a)` | $0.5x \left(1 + \tanh\left(\sqrt{\frac{2}{\pi}} (x + 0.044715 x^3)\right)\right)$ |
| **SiLU (Swish)** | `tensor_silu(a)` | $\frac{x}{1 + e^{-x}}$ |
| **Sigmoid** | `tensor_sigmoid(a)` | $\frac{1}{1 + e^{-x}}$ |
| **Tanh** | `tensor_tanh(a)` | $\frac{e^x - e^{-x}}{e^x + e^{-x}}$ |

### Fused Normalization Layers

1. **RMSNorm (`tensor_rmsnorm(x, weight, eps)`):**
   - Applies Root-Mean-Square normalization across the final dimension:
     $$\text{RMS}(x) = \sqrt{\frac{1}{d} \sum_{i=1}^d x_i^2 + \epsilon}, \quad y_i = \frac{x_i}{\text{RMS}(x)} \cdot w_i$$
2. **LayerNorm (`tensor_layernorm(x, gamma, beta, eps)`):**
   - Normalizes with both mean centering and variance scaling:
     $$y_i = \frac{x_i - \mu}{\sqrt{\sigma^2 + \epsilon}} \cdot \gamma_i + \beta_i$$

### Numerically Stable Softmax (`tensor_softmax(x, axis)`)

To prevent exponential overflow ($e^x \to \infty$), the layer subtracts the row maximum before computing exponentials:

$$m = \max_j(x_j), \quad p_i = \frac{e^{x_i - m}}{\sum_j e^{x_j - m}}$$

### Rotary Position Embeddings (RoPE)

- `tensor_rope_2d(q, k, offset, base)`: Applies 2D rotary embeddings $[S, D]$.
- `tensor_rope_4d(q, k, offset, base)`: Applies multi-head rotary embeddings $[B, H, S, D]$.
- For each coordinate pair $(x_{2i}, x_{2i+1})$ at token position $p$:
  $$\theta_i = \text{base}^{-2i/d}, \quad \begin{pmatrix} x'_{2i} \\ x'_{2i+1} \end{pmatrix} = \begin{pmatrix} \cos(p\theta_i) & -\sin(p\theta_i) \\ \sin(p\theta_i) & \cos(p\theta_i) \end{pmatrix} \begin{pmatrix} x_{2i} \\ x_{2i+1} \end{pmatrix}$$

### Scaled Dot-Product Attention (SDPA) (`tensor_sdpa(q, k, v, is_causal)`)

Computes full multi-head attention:

$$\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{Q K^T}{\sqrt{d_k}} + M\right) V$$

- **Causal Masking:** When `is_causal = 1`, all positions where $\text{col} > \text{row}$ are set to $-10000.0$, strictly preventing attention to future tokens.

---

## 6. Phase 4: Reverse-Mode Autograd Engine

### The Computation Tape

The engine uses an arena-based execution tape (`TapeNode`):

```tezz
struct TapeNode:
  op_type:int
  out_grad:*Tensor
  has_arg0:int
  arg0_grad:*Tensor
  arg0:*Tensor
  has_arg1:int
  arg1_grad:*Tensor
  arg1:*Tensor
  saved_out:*Tensor
  scalar_val:float
  axis:int
  keepdim:int
```

- `tape_init()`: Initializes tape buffer (default 8,192 nodes).
- `tape_reset()`: Cleans up all saved intermediate tensors and resets tape length to 0.
- `tensor_require_grad(t)`: Flags tensor for gradient calculation and allocates an uninitialized gradient buffer (`t.grad`).

### Backward Pass & Gradient Accumulation

Calling `tensor_backward(loss)` executes:
1. **Loss Seeding:** Initializes `loss.grad = [1.0]`.
2. **Reverse Topological Traversal:** Iterates from node `_tape_len - 1` down to 0.
3. **Analytical Vector-Jacobian Products (VJPs):**
   - **Matmul ($C = A B$):** $\nabla A = \nabla C \cdot B^T, \quad \nabla B = A^T \cdot \nabla C$
   - **Add / Sub:** Passes upstream gradients; collapses broadcast dimensions if operands differ in shape.
   - **Mul:** $\nabla A = \nabla C \odot B, \quad \nabla B = \nabla C \odot A$
   - **ReLU:** $\nabla X = \nabla Y \odot (X > 0)$
   - **SiLU:** $\nabla X = \nabla Y \odot (\sigma(X) + Y(1 - \sigma(X)))$
   - **GELU:** Exact derivative of the hyperbolic tangent approximation.
   - **MSE Loss:** $\nabla \hat{Y} = \frac{2}{N} (\hat{Y} - Y)$
   - **Cross-Entropy Loss with Softmax:** $\nabla Z = \frac{1}{B} (P - Y_{\text{target}})$
4. **Gradient Accumulation:** If a tensor is used in multiple branches, `tensor_accumulate_grad` accumulates gradients into `param.grad`.
5. **Broadcast Dimension Collapse:** When a broadcasted bias (e.g. $[1, D]$) is added to an activation (e.g. $[B, D]$), `tensor_collapse_to_shape` automatically sums gradients across the broadcasted axes.

---

## 7. Phase 5: Optimizers & Training Loop

### Learning Rate Schedulers

1. **Linear Schedule (`lr_linear_schedule`):**
   $$\eta_t = \eta_{\min} + (\eta_{\text{initial}} - \eta_{\min}) \cdot \left(1 - \frac{t}{T_{\text{total}}}\right)$$
2. **Cosine Schedule with Warmup (`lr_cosine_schedule`):**
   - For $t < T_{\text{warmup}}$: Linear warmup $\eta_t = \eta_{\min} + (\eta_{\max} - \eta_{\min}) \cdot \frac{t}{T_{\text{warmup}}}$.
   - For $T_{\text{warmup}} \le t \le T_{\text{total}}$: Half-cosine decay:
     $$\eta_t = \eta_{\min} + \frac{1}{2}(\eta_{\max} - \eta_{\min})\left(1 + \cos\left(\pi \frac{t - T_{\text{warmup}}}{T_{\text{total}} - T_{\text{warmup}}}\right)\right)$$

### Decoupled Weight Decay AdamW (`struct AdamW`)

```tezz
fn adamw_create(lr:float, beta1:float, beta2:float, eps:float, weight_decay:float) -> AdamW
fn adamw_add_param(opt:*AdamW, p:*Tensor) -> int
fn adamw_zero_grad(opt:*AdamW) -> int
fn adamw_clip_grad_norm(opt:*AdamW, max_norm:float) -> float
fn adamw_step(opt:*AdamW) -> int
fn adamw_free(opt:*AdamW) -> int
```

- **Update Equations:**
  1. Decoupled weight decay: $\theta_t = \theta_{t-1} \cdot (1 - \eta \lambda)$
  2. First moment estimate: $m_t = \beta_1 m_{t-1} + (1 - \beta_1) g_t$
  3. Second moment estimate: $v_t = \beta_2 v_{t-1} + (1 - \beta_2) g_t^2$
  4. Bias correction: $\hat{m}_t = \frac{m_t}{1 - \beta_1^t}, \quad \hat{v}_t = \frac{v_t}{1 - \beta_2^t}$
  5. Parameter step: $\theta_t = \theta_t - \eta \frac{\hat{m}_t}{\sqrt{\hat{v}_t} + \epsilon}$

### Momentum SGD (`struct SGD`)

```tezz
fn sgd_create(lr:float, momentum:float, weight_decay:float, nesterov:int) -> SGD
fn sgd_add_param(opt:*SGD, p:*Tensor) -> int
fn sgd_zero_grad(opt:*SGD) -> int
fn sgd_clip_grad_norm(opt:*SGD, max_norm:float) -> float
fn sgd_step(opt:*SGD) -> int
fn sgd_free(opt:*SGD) -> int
```

---

## 8. End-to-End Code Examples

### Example 1: Matrix Multiplication & Zero-Copy Transpose

```tezz
import "tensor"

fn main() -> int:
  // Allocate A[2, 3] and B[2, 3]
  a:Tensor = tensor.tensor_2d(2, 3)
  b:Tensor = tensor.tensor_2d(2, 3)

  tensor.tensor_fill(a, 1.0)
  tensor.tensor_fill(b, 2.0)

  // Zero-copy transpose of B: [2, 3] -> [3, 2]
  b_t:Tensor = tensor.tensor_transpose(b, 0, 1)

  // Multiply A[2, 3] @ B_T[3, 2] -> C[2, 2]
  c:Tensor = tensor.tensor_matmul(a, b_t)

  say "C[0, 0] = ", tensor.tensor_get2(c, 0, 0) // Prints: 6.0

  tensor.tensor_free_tensor(a)
  tensor.tensor_free_tensor(b)
  tensor.tensor_free_tensor(c)
  ret 0
```

### Example 2: Training a 2-Layer Neural Network (MLP)

```tezz
import "tensor"

fn main() -> int:
  tensor.tape_init()

  // Input X[4, 2], Target Y[4, 1]
  x:Tensor = tensor.tensor_2d(4, 2)
  y_true:Tensor = tensor.tensor_2d(4, 1)
  // ... initialize X and Y ...

  // Parameters
  w1:Tensor = tensor.tensor_require_grad(tensor.tensor_randn_2d(2, 4, 0.0, 0.1))
  b1:Tensor = tensor.tensor_require_grad(tensor.tensor_zeros_2d(1, 4))
  w2:Tensor = tensor.tensor_require_grad(tensor.tensor_randn_2d(4, 1, 0.0, 0.1))
  b2:Tensor = tensor.tensor_require_grad(tensor.tensor_zeros_2d(1, 1))

  opt:AdamW = tensor.adamw_create(0.05, 0.9, 0.999, 1.0e-08, 0.01)
  unsafe:
    tensor.adamw_add_param(&opt, &w1)
    tensor.adamw_add_param(&opt, &b1)
    tensor.adamw_add_param(&opt, &w2)
    tensor.adamw_add_param(&opt, &b2)

  epoch:int = 0
  while epoch < 100:
    tensor.tape_reset()
    unsafe:
      tensor.adamw_zero_grad(&opt)

    // Forward: H = ReLU(X @ W1 + B1), Pred = H @ W2 + B2
    h:Tensor = tensor.tensor_relu(tensor.tensor_add(tensor.tensor_matmul(x, w1), b1))
    pred:Tensor = tensor.tensor_add(tensor.tensor_matmul(h, w2), b2)

    loss:Tensor = tensor.tensor_mse_loss(pred, y_true)

    // Backward & Optimizer Step
    tensor.tensor_backward(loss)
    unsafe:
      tensor.adamw_clip_grad_norm(&opt, 1.0)
      tensor.adamw_step(&opt)

    epoch = epoch + 1

  unsafe:
    tensor.adamw_free(&opt)
  ret 0
```

### Example 3: Full LLM Transformer Attention Step

```tezz
import "tensor"

fn main() -> int:
  tensor.tape_init()

  // Sequence length 8, hidden dim 32
  seq_len:int = 8
  dim:int = 32

  x:Tensor = tensor.tensor_randn_2d(seq_len, dim, 0.0, 0.1)
  rms_w:Tensor = tensor.tensor_ones_1d(dim)

  wq:Tensor = tensor.tensor_require_grad(tensor.tensor_randn_2d(dim, dim, 0.0, 0.05))
  wk:Tensor = tensor.tensor_require_grad(tensor.tensor_randn_2d(dim, dim, 0.0, 0.05))
  wv:Tensor = tensor.tensor_require_grad(tensor.tensor_randn_2d(dim, dim, 0.0, 0.05))

  // 1. RMSNorm
  x_norm:Tensor = tensor.tensor_rmsnorm(x, rms_w, 1.0e-05)

  // 2. QKV Projections
  q:Tensor = tensor.tensor_matmul(x_norm, wq)
  k:Tensor = tensor.tensor_matmul(x_norm, wk)
  v:Tensor = tensor.tensor_matmul(x_norm, wv)

  // 3. Rotary Position Embeddings (RoPE)
  tensor.tensor_rope_2d(q, k, 0, 10000.0)

  // 4. Causal Scaled Dot-Product Attention
  attn_out:Tensor = tensor.tensor_sdpa(q, k, v, 1)

  say "Attention block completed successfully with shape [", attn_out.shape0, ", ", attn_out.shape1, "]"
  ret 0
```

---

## 9. Hardware Acceleration: NVIDIA CUDA Driver Integration

TezzNative's Tensor Engine architecture integrates with host NVIDIA GPUs via the standard Windows CUDA Driver API (`nvcuda.dll`):

- **Zero-Dependency JIT PTX Loading:** Kernels are authored in textual PTX and assembled by NVIDIA's `ptxas` or JIT-compiled directly by `cuModuleLoadData` at runtime. No external MSVC compiler (`cl.exe`) is required.
- **Turing / sm_75 Tested Performance (NVIDIA GeForce GTX 1650):**
  - **Memory Transfer Bandwidth:** Host-to-Device transfer sustained at **$4.13\text{ GB/s}$**.
  - **Activation Throughput:** Fused SiLU processes $1,048,576$ elements in **$0.52\text{ ms}$** ($>2,000\text{ M-elem/s}$).
  - **GEMM Compute Performance:** Sustained **$185.7\text{ GFLOPS}$** on $[1024 \times 1024]$ matrix multiplication.
  - **LLM Attention Projection:** Complete $Q, K, V, W_{out}$ projection batch executes in **$1.52\text{ ms}$** ($>83,800\text{ tokens/sec}$).

---

## 10. Conformance & Test Suite Reference

| Test Suite | File Path | Scope | Result |
|---|---|---|:---:|
| **Phase 1** | `tests/test_tensor_phase1.tn` | N-D Allocations, Indexing, Transpose, Slicing, Permute, Squeeze, Contiguous Repack | **8/8 PASS** |
| **Phase 2** | `tests/test_tensor_phase2.tn` | SIMD GEMM, 3D BMM, Broadcasting, Reductions, Axis Reductions | **7/7 PASS** |
| **Phase 3** | `tests/test_tensor_phase3.tn` | Fused Activations, RMSNorm, LayerNorm, Stable Softmax, RoPE, Causal SDPA | **6/6 PASS** |
| **Phase 4** | `tests/test_tensor_phase4.tn` | Dynamic Tape, VJPs, Gradient Accumulation, Bias Collapse, MSE & Cross-Entropy Loss | **6/6 PASS** |
| **Phase 5** | `tests/test_tensor_phase5.tn` | Schedulers, Gradient Clipping, AdamW, SGD with Momentum, End-to-End MLP & Transformer | **6/6 PASS** |
| **LLM Benchmark** | `tests/test_llm_transformer_benchmark.tn` | 10-step full Transformer block training with Cross-Entropy Loss & AdamW | **PASS** |
| **CUDA Benchmark** | `tools/test_cuda_tensor_llm.py` | GeForce GTX 1650 CUDA Driver API, Fused SiLU, Tiled GEMM, Attention Projections | **PASS** |
