# Tensor and SDK AI compatibility

The SDK-only `ai`, `autograd`, `grad`, `ml`, `optim`, `modelio`, and `tensor_gpu` modules use the current public tensor engine. The public engine stores TezzNative `float` values as IEEE binary64. The separate `tztensor` family uses opaque binary32 handles; these layouts are not interchangeable.

## Explicit CPU tensor conversion

`tensor_bridge.to_tztensor(public_tensor)` creates an owning contiguous f32
handle. `tensor_bridge.to_tensor(handle)` creates an owning public f64 tensor.
Both preserve rank 1–4 and dimensions. Public positive-stride transpose/slice
views and zero-stride broadcasts are copied in logical element order. The
source must remain alive during conversion; successful results own separate
storage and can outlive the source. Free the result with its engine's normal
tensor-free function.

The source layouts and autograd tapes remain distinct. Ordinary conversions
reject any source with `requires_grad != 0`. The explicitly named
`to_tztensor_detached` and `to_tensor_detached` variants copy values while
discarding gradient history; their results have no gradients or tape links.
They do not transfer saved gradients or connect backward across the engines.

The CPU conversion uses the language's IEEE f32 rounding (nearest-even in the
supported default execution environment). Narrowing can lose precision or
round tiny values to subnormal/zero. A finite value that would become infinity
is rejected, including partial-output cleanup. Existing infinities, NaN
categories and signed zero are preserved; NaN payload identity is not promised.
Widening finite f32 values to f64 is exact.

Null inputs, unsupported rank/dtype/device, inconsistent shapes and index
arithmetic overflow fail explicitly: f32 conversion returns handle zero; f64
conversion returns an empty tensor (`size == 0`, null data). GPU handles are
rejected without downloading them or changing the selected device. As with
existing tensor APIs, metadata checks cannot make forged pointers or expired
allocations valid.

Verification includes independent Python IEEE bit cases and real CPU
bytecode/native execution, plus 200 SGD updates with analytic gradients after
f64 input conversion. Run `tests/run.py --filter tensor_bridge --compiler PATH`
and `tests/regression/tensor_bridge/check_numeric.py --compiler PATH`.

## Tensor ownership and autodiff

The legacy `ml.tensor_new`, arithmetic, reduction, view, and model functions retain their `*Tensor` interfaces. The SDK's `tensor_ptr` adapter allocates one owning header per returned result. Release these headers with `ml.tensor_free` or `tensor_ptr.release`. A view borrows its base storage; keep the base alive until all views are released. Call `tensor.tape_reset` when a graph is no longer needed to release saved tape state.

Legacy `autograd.ag_*` operations share the public tape. Backward follows the requested result, clears intermediate adjoints, and accumulates leaf gradients across repeated calls. Zero leaf gradients explicitly when accumulation is unwanted. `ag_grad` returns a borrowed gradient view.

Scalar `autodiff.free_node` frees every distinct node reachable from its root once. Child pointers and other roots sharing those nodes become invalid after that call.

## Supported execution and numeric APIs

`ml.backend_cpu()` returns zero. `ml.backend_gpu()` and `ml.backend_npu()` return a negative status: these calls cannot select another storage engine. `ml.tensor_new_dtype` accepts only the public f64 dtype. Legacy integer and boolean storage constructors return null.

`tensor_gpu.add` and `tensor_gpu.mul` upload equal-shaped f64 tensors to the real CUDA library and return owning CPU tensor results. They return null if the backend, allocation, transfer, or computation fails. They do not select a CPU fallback. Their backward rules use the CPU tensor tape. The complete CUDA source now has real GTX 1650 verification on Linux/WSL; other devices and the Windows CUDA build remain separate verification targets.

The public library now also distributes `tztensor`, `tzautodiff`, `tzcpu`, and
`tzgpu`, so `tensor_bridge` resolves from both the repository and an isolated
package installation. The f32 and f64 storage contracts stay distinct.

The f32 linear, ReLU, GELU, convolution and max-pool entry points validate
shapes and devices before writing output and propagate kernel/tape failures.
Convolution and max-pool currently require CPU storage and reject GPU handles;
there is no implicit download or CPU execution of device pointers. Max-pool
masks remain caller-owned and must stay alive through backward.

`npu` has one typed native/VM ABI contract. Default builds return
`available() == 0`, `backend_name() == "none"`, and explicit execution errors.
Optional model adapters now target Intel OpenVINO NPU, AMD VitisAI NPU,
Qualcomm QNN HTP and attached Hailo devices. They require a compatible vendor
SDK/driver and dedicated hardware; no physical NPU execution is verified.
Contract tests and real-runtime absence checks do not establish model support
on an NPU. Raw allocation/kernel APIs and `smart_matmul` remain unavailable.
CPU SIMD, CPU ONNX and DirectML GPU are never selected as substitutes. The
legacy `TN_NPU_ONNXRUNTIME` macro still fails compilation because its previous
implementation selected CPU execution. See [provider contracts](NPU_PROVIDERS.md)
for exact model, precision, build and verification boundaries.

CPU vision kernels check window/count arithmetic before division or memory
access. Max-pool and top-k select real input values even below -1e30. Masked
softmax writes exact zero to disabled entries and to fully masked rows. CUDA
exports have independent real-device numerical and error checks, including
GELU/normalization derivatives, GEMM variants, optimizers and a 200-step linear
fit. CPU convolution/max-pool support does not imply GPU convolution/max-pool.

The CUDA library uses native tiled GEMM and deterministic counter-based device
RNG. The RNG sequence differs from the old cuRAND implementation; it is not
cryptographic. Numerical checks do not claim cuBLAS performance parity.
SDK GPU convenience functions propagate device failures. Transformer forward
normalizes owned copies, preserving caller input and residual values. Actual
MLP training arithmetic and these error paths pass both bytecode and native
GPU execution; large-model convergence and long-duration stability remain
separate work.

Historical raw APIs named `*_f32` in `ml` and `tensor_mt` take `*float`, which is binary64 in TezzNative. Their names do not change that ABI. Use the distinct `tztensor` APIs for binary32 storage.

The byte tokenizer adapter supports encode/decode and bounded output buffers. Vocabulary fitting and legacy tokenizer checkpoint saving return a negative status because the current tokenizer has no implementation for those operations. `tok_load` reads the current vocabulary format without a merge file.

## Model files

The TNX decimal parser uses a host runtime conversion primitive with a 1 MiB limit per numeric span. It preserves signed zero and subnormals, accepts finite underflow to zero, and rejects overflow/nonfinite values. Windows requires the system Universal C Runtime (UCRT), included in the supported Windows 10/11 platforms. Linux uses POSIX locale objects and `strtod_l`. The verified bindings target x64 BC and native execution.

`tensor_save_tnxb` writes schema `tezz_tensor_v2` with the current f64 dtype identifier and binary64 TNX-B data. Loading requires exact rank, shape, and data counts with numeric entries. Legacy `tezz_tensor_v1` files with floating dtype zero are promoted to f64; legacy integer/boolean storage formats are rejected. Dense model files keep schema `tezz_dense_v1`.

Run `python tests/run.py --compiler PATH --suite regression --filter sdk_legacy_ai --backend both` for deterministic numeric, ownership, serialization, and unsupported-backend checks.

