# Dedicated NPU providers

NPU support is opt-in. Default builds report no backend and fail execution
explicitly. Intel, AMD and Qualcomm adapters have been compiled against pinned
official C APIs and tested with real vendor runtimes on a machine without an
NPU. Provider-contract test doubles cover successful inference and failure
cleanup. **No physical NPU execution has been verified.** These are experimental
model adapters, not complete device-family certification.

## Model and precision contract

Each runtime build selects one provider. Availability requires an enumerated
dedicated NPU, not merely an installed runtime library or execution provider.
Intel/ORT model loading requires one input and one output, static positive dimensions,
rank 1 through 8, and f32 model ports. Dynamic shapes, multiple inputs/outputs
and other port dtypes fail explicitly. The public `npu.model_run` interface
flattens each port and requires exact element counts.

Public buffers contain f64 values. Input is checked for nonfinite values and
f32 overflow, then explicitly narrowed; finite outputs are widened to f64.
This does not promise f64 computation inside the NPU. Provider-internal
precision and supported operators remain hardware/model dependent. A failed
run leaves the caller's output unchanged. Handles are invalidated on free or
shutdown and never recycled during the process lifetime.

The adapters serialize lifecycle and inference calls. Raw `alloc`, transfer,
kernel launch and DXIL functions remain unsupported; they do not allocate CPU
memory while presenting it as NPU memory. `smart_matmul` remains unavailable.

## Intel first

Configure the SDK with `-DTN_NPU_OPENVINO=ON` and `-DOpenVINO_DIR=.../cmake`.
Verification uses OpenVINO 2025.4.1. The adapter enumerates only `NPU`/`NPU.N`,
compiles exclusively for the selected device and verifies that execution lists
one NPU. It never selects `AUTO`, `HETERO`, CPU or GPU. Models may use formats
supported by the installed OpenVINO frontend, including IR and ONNX.

```sh
python -m pip install --no-deps --target build/deps/openvino openvino==2025.4.1
python tests/regression/npu/check_openvino.py
python tests/regression/npu/check_openvino.py --require-device --out build/verification/intel-device
```

The last command fails on a machine without an Intel NPU. A pass verifies the
owned f32 ReLU fixture only, not arbitrary models or all Intel generations.
The required driver and supported precision are described in the
[Intel NPU documentation](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html).

## AMD, then Qualcomm

Use `-DTN_NPU_ORT_AMD=ON` or `-DTN_NPU_ORT_QNN=ON`, plus `-DTN_ORT_ROOT=...` for
an ONNX Runtime C SDK providing API version 23 or later. The adapter requests
API 23; verification pins headers and runtime to 1.23.2. Explicit
`TN_ORT_INCLUDE_DIR` and `TN_ORT_LIBRARY` CMake paths may be supplied instead.
The vendor runtime and plugin must support the same ABI and target platform.

Set `TN_NPU_EP_LIBRARY` to the absolute path of a locally installed compatible
vendor plugin if it is not already registered by the runtime. It is read at
initialization; no runtime download or automatic provider substitution occurs.
Legacy provider packages without plugin device enumeration are unsupported.

AMD selects only `VitisAIExecutionProvider` devices whose hardware type is NPU,
using the vendor's
[explicit device-selection API](https://ryzenai.docs.amd.com/projects/WinML/en/latest/winml_ep.html).
Qualcomm selects only `QNNExecutionProvider` NPU devices, forces the HTP backend
and disables CPU offloading of graph I/O quantization. Both set
`session.disable_cpu_ep_fallback=1`; unsupported graphs fail session creation.
Host buffers transfer model input/output and are not a CPU execution provider.
See the [QNN provider contract](https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html)
for supported models, precision and platform requirements.

```sh
python -m pip install --no-deps --target build/deps/onnxruntime onnxruntime==1.23.2
python tests/regression/npu/check_ort.py --provider AMD --fetch-headers --out build/verification/npu-amd
python tests/regression/npu/check_ort.py --provider QNN --out build/verification/npu-qnn
```

Add `--require-device` on a vendor-equipped runner to require actual execution.
Without that flag, absence is tested and reported as `unverified_no_hardware`.
Installing generic ONNX Runtime alone does not install an AMD/Qualcomm NPU
plugin or establish device support. The test-generated ReLU ONNX contains no
downloaded weights.

## Raspberry Pi attached Hailo accelerators

Use `-DTN_NPU_HAILORT=ON` with `TN_HAILORT_ROOT`, or explicit
`TN_HAILORT_INCLUDE_DIR`/`TN_HAILORT_LIBRARY` paths, for a compatible HailoRT C
SDK. The adapter targets an attached physical Hailo device; a Raspberry Pi CPU
or the Pi 3B QEMU profile does not provide this accelerator.

The initial contract is compiled against HailoRT 4.20.0 official headers. It
selects one scanned device ID, loads a local HEF with one configured network
group/input/output and runs one frame synchronously. Fixed NHWC/NCHW/NC host
layouts are supported; NMS, packed images and other layouts are rejected.
Host buffers use f32 with explicit conversion from/to the public f64 interface.
Device arithmetic follows the quantized HEF; f32 host I/O does not establish
f32 arithmetic on the NPU. No generic raw memory or shader API is supplied.

Linux ASan/UBSan/leak checks and an ARM64 QEMU run verify the official-header
test double, including 11 model-load and three run failure points. The real
HailoRT 4.20.0 shared library was also built from pinned unmodified source and
linked on Linux x64. With its driver absent, the adapter correctly reports
zero devices and rejects initialization, model loading and inference without
changing caller output. Vendor/source hashes and dependency revisions are
recorded in the verification artifacts.

The gate is `tests/regression/npu/check_hailo.py --fetch-headers --cross-aarch64`.
Supply `--library /path/to/libhailort.so.4.20.0` to include real-library absence
verification. No Hailo device, driver execution or HEF compatibility is
verified. `--require-device` fails because this gate cannot certify inference.
New Hailo generations and other attached accelerators need separate SDK/device
verification rather than inheriting this status.

Apple ANE and other NPU families have no implemented provider in this milestone.
An OS/CPU brand, an installed driver, or a reserved public constant does not
enable them. They remain explicit outstanding targets.

## Verification boundaries

Intel contract tests inject failures at 12 model-load and 16 inference API
calls, including replacement of output storage. AMD and Qualcomm each inject
20 model-load and four inference failures. Selection, stale handles, malformed
shapes, wrong dtypes, finite conversion boundaries and output preservation are
checked against actual vendor headers. Linux contract runs enable ASan/UBSan
with leak detection. Windows/Linux real-runtime absence tests pass for Intel,
AMD and Qualcomm. AMD/Qualcomm owned ONNX fixtures also pass the vendor parser.
Deploy the chosen vendor C runtime beside Windows executables (or with an
explicit Linux loader path); a different system-installed runtime may have an
older incompatible API. Linux deployment also needs the runtime's SONAME
links, including `libonnxruntime.so.1`; the Python wheel omits that link.
Use the complete C SDK layout or stage the required runtime name explicitly.
Verification stages and hashes its runtime explicitly.

CI preserves contract and device statuses separately. An adapter build, a
mock inference, an emulator pass and a physical-device pass are distinct
evidence. No result here establishes universal OS/CPU/NPU support, speech
quality, model convergence or performance parity with a vendor's optimized
application stack.
