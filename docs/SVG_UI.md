# SVG icons in TezzNative UI

`tzsvg` loads an SVG file or SVG text into a reusable vector asset. It flattens
curves once at load time, so rendering an icon each frame does not reopen or
reparse the file.

```tn
import "tzsvg"

icon:*TzSvgAsset = tzsvg.svg_load_file("assets/search.svg")
if icon != 0:
  // Draw into a GpuCanvas; -1 keeps colors from the SVG source.
  tzsvg.svg_draw(icon, &app.ctx.canvas, 24, 24, 24, 24, 0 - 1)
  // A non-negative color tints both fill and stroke.
  tzsvg.svg_draw(icon, &app.ctx.canvas, 56, 24, 24, 24, 0xE2E8F0)
  tzsvg.svg_free(icon)
```

An icon can also be downloaded at runtime and parsed from the response body:

```tn
import "net"
import "tzsvg"

svg_text:str = net.http_fetch_body(
  "https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/search.svg",
  10000,
  65536)
if svg_text != 0:
  icon:*TzSvgAsset = tzsvg.svg_load_text(svg_text)
  free(svg_text as *char)
```

The renderer is for app icons and small vector UI artwork. It supports SVG
`viewBox`, inherited `fill` and `stroke` styles, style declarations, common
basic shapes, and path commands `M L H V C S Q T A Z` (both relative and
absolute). Gradients, filters, masks, external CSS, text, and arbitrary
transforms need a full browser SVG renderer and are not interpreted by this
icon loader.

`examples/dx/gpu_hello_ui.tn` loads `examples/dx/assets/tezz-mark.svg` once and
draws it in the application header as a working usage example.
