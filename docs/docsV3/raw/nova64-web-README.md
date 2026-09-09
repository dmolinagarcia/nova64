# noVa64 in a browser

The emulator compiles to WebAssembly and runs client-side. Nothing executes on
the server, so a plain static host is enough.

```
make wasm          # build web/nova64.wasm
make wasm-test     # run it under node, no browser
make serve         # http://localhost:8080
```

## You may not need to build it

`nova64.wasm` is a static artifact that changes only when the emulator's C
changes, which is rare next to how often a boot image changes. A machine with
no wasm toolchain can serve and run the console perfectly well with a prebuilt
module dropped into `web/`.

The hazard with committing a build output is that it drifts from the source
and someone spends an hour debugging a change that was never compiled in.
`make wasm-check` guards that, and `make serve` runs it first:

```
$ make wasm-check
web/nova64.wasm is older than these sources:
  src/cpu.c
Run 'make wasm' where a wasm toolchain is available.
```

So: build the module wherever you have a modern clang, commit it, and let the
development machine handle only `boot.s` — which needs cc65, not clang.

## Matching the sysroot to your linker

`wasm-ld` reads the sysroot's object files, and the wasm object format has
changed over the years, so a linker much older than the sysroot will refuse
it:

```
wasm-ld: error: /opt/wasi-sysroot/lib/wasm32-wasi/crt1.o: Invalid symbol type
```

Two things fix this, and the first is the real one.

**The module is linked with `-nostartfiles`.** It has no entry point, so
linking a startup object was wrong to begin with: `crt1.o` exists to call
`main`, which this module does not have. Dropping it removes the object that
old linkers choke on, and takes the module from 346 KB to 97 KB because the
argument and environment machinery goes with it.

**If something else still refuses, match the release to your LLVM.** wasi-sdk
11 was built with LLVM 10, 12 with LLVM 11, and so on upward. Substitute the
version in both download URLs; the emulator only uses stdio, stdlib, string
and math, so a libc of any vintage covers it. Builds against wasi-sdk 11 and
25 produce byte-identical behaviour here.

## If the build fails

`clang: error: unknown argument: '-mexec-model=reactor'` meant a clang built
without the WebAssembly target: that option belongs to the wasm target, so a
clang that lacks it reports the option as unknown rather than the target as
missing. The Makefile no longer uses that option at all, and checks the target
up front instead. If you see the check fire, install a clang that carries
wasm32 — the Debian and Ubuntu packages do:

```
apt install clang-18 lld-18
make wasm WASM_CC=clang-18
```

Don't diagnose this with `clang -print-targets`: that option only exists from
clang 13, so it fails on exactly the old compilers you would be checking. The
Makefile probes by compiling a trivial file for `wasm32` instead, which works
on every version, and prints what your compiler reports itself as when the
probe fails.

`make wasm` needs a WASI sysroot:

```
curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/wasi-sysroot-25.0.tar.gz
tar xzf wasi-sysroot-25.0.tar.gz -C /opt && mv /opt/wasi-sysroot-25.0 /opt/wasi-sysroot
```

and the wasm32 compiler-rt builtins from the same release, dropped into
`$(clang -print-resource-dir)/lib/wasi/`:

```
curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/libclang_rt.builtins-wasm32-wasi-25.0.tar.gz
tar xzf libclang_rt.builtins-wasm32-wasi-25.0.tar.gz
mkdir -p "$(clang -print-resource-dir)/lib/wasi"
cp libclang_rt.builtins-wasm32-wasi-25.0/libclang_rt.builtins-wasm32.a \
   "$(clang -print-resource-dir)/lib/wasi/"
```

The module is linked with `--no-entry` rather than as a reactor, so it exports
`__wasm_call_ctors` instead of `_initialize`. The JS hosts call whichever one
is present, so either build works.

Emscripten works too and is the easier route if you already have it:

```
emcc -DNOVA_NO_PTY -std=c11 -O2 -Iinclude \
  $(ls src/*.c | grep -v main.c) -o web/nova64.js \
  -sEXPORTED_FUNCTIONS=$(sed -n 's/^EXPORT[^ ]* \**\([a-z_]*\)(.*/_\1/p' src/wasm_api.c | paste -sd, -) \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728
```

The shell in `index.html` targets the WASI build directly; switching to the
emcc output means replacing the `instantiateStreaming` block with the
generated loader and reading `HEAPU8.buffer` instead of `memory.buffer`.

## What the browser supplies

**A live pixel window.** The canvas replaces the `.pgm` sequence. This is the
one host output the native build could not provide.

**Pointer lock, which is the capture requirement.** DN-SW-EMU-001 section 6.4
argues that capture is functional rather than convenient under a relative
motion protocol, because an uncaptured host pointer stops producing deltas at
a screen edge. Pointer lock is exactly that mechanism, and `movementX/Y` is
exactly the delta stream the mailbox carries.

**Physical key identity.** `KeyboardEvent.code` names the key by position, not
by the character it produces, so a Spanish, US or Dvorak host keyboard all
deliver the same scancode. Section 6.3 requires that host layout must not leak
into the guest; `code` gives that for free, where `key` would not.

## Serving it

Any static host. `.wasm` must be served as `application/wasm` for
`instantiateStreaming` to work; Python's `http.server` does this correctly on
3.11 and later. No COOP/COEP headers are needed: the emulator is
single-threaded and uses no `SharedArrayBuffer`.

## Measured

Under node, after JIT warm-up, on the text-mode workload:

```
23.2 Mcycle/s
2.9x realtime at 8 MHz
0.5x realtime at 50 MHz
```

Comfortable for the 8 MHz target with headroom to spare. A 50 MHz softcore
would not run in real time without optimisation work.

Two things dominate and neither is the CPU core:

- `render_text()` repaints all 9600 character cells on every PRESENT, which is
  614,400 pixel writes per frame. It needs dirty-cell tracking before anything
  interactive.
- `nova_render()` converts the whole framebuffer to RGBA every frame. It should
  convert only what changed.

## What has actually been tested

`make shell-test` loads `index.html` into jsdom with stubs for the pieces
jsdom lacks, then drives the real flow: load an image, press Run, run frames,
capture input, type, move the mouse. 22 checks, all passing.

It verifies that the tee delivers 5462 characters matching the native build,
that the canvas receives a 1024x600 frame with 57,862 lit pixels matching the
native render exactly, that the readouts advance, and — reading the machine's
own HID counter rather than checking that nothing threw — that a mapped key
delivers two mailbox events, an unmapped key delivers none, pointer motion
delivers an event while captured and is dropped once capture is released.

## What still needs a real browser

jsdom is not a browser, and the stubs are the parts most likely to be wrong:

- **The 2D canvas is a stub.** `putImageData` is recorded, not rendered.
  Nothing here proves the frame looks right on screen, that
  `image-rendering: pixelated` behaves, or that scaling a 1024x600 canvas
  down by CSS stays legible at 8-pixel glyphs. That last one is the most
  likely thing to be wrong.
- **Pointer lock is a stub.** The real API is asynchronous, requires a user
  gesture, and browsers differ on `movementX/Y` scaling and on acceleration.
  The delta stream reaching the guest may not be what this harness suggests.
- **Key events are synthesised.** A real browser fires them with modifiers,
  repeat, and IME involvement, and `preventDefault` interacts with browser
  shortcuts. Some `code` values will be swallowed by the browser before the
  page sees them.
- **CSS layout is not evaluated.** jsdom parses the stylesheet and applies
  nothing.
- **`instantiateStreaming` is fed a synthetic Response.** A real server has to
  send `application/wasm` or it silently falls back or fails.

So the wiring is verified and the presentation is not.
