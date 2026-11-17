# TezzNative Language Specification

TezzNative ships two CLI execution surfaces:
- `systems` mode is the compatibility-first default.
- `simple` mode is a script-first surface layered on top of the same parser, sema, and runtime.

## Lexical Structure
- Source files are UTF-8 text; production fixtures in this repo stay ASCII by convention.
- Significant indentation is used for blocks after `:` in the same style as existing `.tn` sources.
- Line comments use `//` and block comments use `/* ... */`.
- String literals use double quotes. Integer literals accept 64-bit magnitudes through `18446744073709551615` in decimal, hexadecimal, or binary; larger magnitudes are errors. Their bits default to signed machine integers (for example `9223372036854775808` has the `int` minimum bit pattern); an explicit unsigned cast interprets the same bits as unsigned. Floating-point literals may exceed the integer magnitude range and support underscore digit separators.
- Identifiers are case-sensitive. Module names are resolved from import paths and file basenames.

## Declarations
- Top-level declarations include `import`, `fn`, `struct`, `enum`, `typedef`, `let`, `extern`, and `static` forms supported by the parser.
- Attributes are written on their own line before a declaration, for example `@abi("c")`.
- `fn main() -> int:` is the required entry point for `check`, `run`, `buildir`, `buildexe`, `abidump`, and release-lane fixtures.

## Types
- Primitive scalar types in the v1 surface include `int`, `float`, `char`, and `void`.
- Pointer-like values include `str`, explicit pointer types such as `*char`, and function-pointer types.
- Aggregate types include fixed-size arrays `[T;N]` and `struct` declarations.
- Type checking is enforced before IR lowering. Production lanes require type errors to stop the build with stable diagnostics.
- Extern and ABI declarations must use ABI-safe signatures; invalid ABI usage is a hard error.
- Integer-to-float conversions interpret `u64` across its full unsigned range and round to nearest, ties to even. Conversion to `f32` rounds the integer directly, avoiding an intermediate `f64` rounding step.
- Float-to-64-bit-integer casts truncate toward zero. Finite signed inputs must lie in `[-2^63, 2^63)` and unsigned inputs in `[0, 2^64)`; NaN, infinities and values outside those intervals produce the integer-indefinite bit pattern `0x8000000000000000`. This is a conversion sentinel and may coincide with a valid integer value.

## Control Flow
- The supported structured control-flow forms in production are `if`, `while`, `for`, `switch`, `ret`, `break`, `continue`, and `unsafe` blocks.
- The formatter canonicalizes spacing, keyword placement, and type spellings for AST snapshot tests.
- IR generation must preserve source-level control flow without silently replacing user code with stubs.
- Logical `&&` and `||` evaluate left to right, skip the right operand when the left determines the result, and return integer `0` or `1`; numeric zero (including negative floating-point zero) is false.

## Attributes
- `@abi("c")` is only valid on `extern fn` declarations.
- `@inline` is only valid on non-extern functions.
- Pointer-borrowing attributes such as `@mut` participate in sema diagnostics and must fail with actionable messages when aliasing rules are violated.
- Unsupported or misplaced attributes are production-blocking diagnostics.

## Imports And Modules
- Imports are resolved relative to the entry file and repo stdlib layout.
- Freestanding builds may not import hosted modules such as `io`, `net`, `time`, `tls`, `gpu`, or `npu`.
- Hosted lanes in this repo exercise parser, sema, formatter, IR, BC-VM execution, and native build paths against real manifests instead of placeholder single-file smoke tests.

## GUI Support
- GUI primitives in `lib/gui.tn`, `lib/tzgui.tn`, `lib/tzui.tn`, and `lib/tnui.tn` are part of the hosted language surface for production candidates.
- The hosted GUI contract includes a native **host-window backend** (SDL2-backed when available) and must report capabilities honestly when a host window cannot be initialized.
- Headless hosts must degrade cleanly: capability detection can return `backend=none` and tests must still pass without fake success paths.
- Runtime GUI gates must include a real window smoke test (`tests/gui_window_smoke.tn`) that presents at least one frame when host windowing is available and gracefully skips on headless hosts.
- A canonical first-app sample with text + buttons must be maintained at `examples/gui_first_app.tn` and compile in GUI check lanes.
- Native control coverage includes retained entry/button plus production-lane checkbox/slider surfaces exposed through `lib/tnui.tn`.
- Input handling must cover pointer/keyboard focus semantics and IME state reporting (`gui_ime_active`, `gui_ime_cursor`, `gui_ime_length`) for hosted backends.
- Compositor-level window management lives in `lib/wm.tn` and must enforce lifecycle operations (create/show/move/resize/remove), focus, z-order, and title-bar drag behavior.
- Compositor runtime behavior is enforced in GUI run manifests (`tests/gui_wm_runtime.tn`) and must pass in BC VM lanes.
- Retained widget-tree operations (`tree_init`, `widget_add`, `widget_update_bounds`, `widget_set_visible`, `widget_remove`, and dirty-region rendering) are covered by production manifests.
- GUI event bridge behavior (`gui_event_push_key` and `gui_event_next`) must remain deterministic for BC-VM test lanes.

## Production Contract
- A release candidate may only be promoted after Linux, Windows, and macOS source builds pass in CI and native SDK archives are produced for each host lane.
- `buildexe` must fail honestly: no PE stub fallback, no placeholder GPU output, and no silent success on unsupported lowering paths.
- Production manifests must contain substantive coverage for lexer/parser, sema, IR snapshots, formatter stability, runtime hardening, buildexe verification, and freestanding checks.
- GUI support must ship with explicit CI enforcement (`tezz test --gui`) and non-empty GUI run/check manifests.
- BC VM/runtime lanes must preserve multi-argument function-call semantics; regression coverage includes `tests/runtime_call_multiarg.tn`.
- Direct diagnostics, release-policy checks, runtime IO smoke, and reproducible-build checks are part of the supported release surface.
