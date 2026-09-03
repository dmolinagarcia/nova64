# Audio — the DAC, the mixer and /dev/audio
> PCM5102A · I2S · four channels · the rate the clock actually gives

Audio is the last block of Neon with no sheet of its own, and it has been specified by accident: a row in [sheet H](sec_ai_h), a line of the pin budget in [sheet T](sec_ai_t), a reserved figure in two resource tables, a priority level in an arbiter, a bank in a memory partition, and four checkboxes in [sheet P](sec_ai_p). None of it is wrong. But nothing anywhere says how many channels there are, what the sample rate is, what a buffer looks like, what happens when one runs dry, or what `/dev/audio` accepts — and one of those questions turns out not to have the answer everybody assumed. This sheet is the whole of it, at REV A. It shares Neon's device, its memory arbiter and its register page with [sheets T](sec_ai_t) and [U](sec_ai_u), and specifies nothing about video.

## Three properties this sheet is built on, and the third is the one that pays for the design.

- W.1 — **The CPU never touches a sample.** It is the same rule the whole of Neon is arranged around ([T.2](sec_ai_t#t2), [J.5](sec_ai_j#j5)), and here it is not a preference but arithmetic. At 8 MHz the 65816 has **181 cycles per stereo frame** at this sheet's rate; mixing four streams costs eight fetches, eight sign extensions, eight volume multiplies on a machine with no multiplier, four saturating adds and two stores. Software mixing is not a slow option on this board, it is **not an available one**, and everything below follows from that.
- W.2 — **A tone depends on nothing.** The I2S serialiser and its divider need no SDRAM, no arbiter, no CPU, no Helium and no bus — only the bitstream, three pins and the DAC. `AUD_TEST` ([W.30](sec_ai_w#w30)) generates a square wave inside the serialiser with no memory access of any kind, which makes audio's first bring-up stage as independent as Mode 0's ([T.1](sec_ai_t#t1)) and puts it in the same place on the build.
- W.3 — **Audio is the cheapest client in the machine, by two orders of magnitude.** 0.7 MB/s against ~150 MB/s of delivered bandwidth, ~500 LUT of 7,680, one block RAM, three pins. Every trade below is decided on logic and on block RAM, never on bandwidth — which is [T.3](sec_ai_t#t3) restated for a block that is smaller than the cursor's neighbours.
  NOTE: **Nothing on this sheet depends on [Q58](sec_ai_q#q58).** The audio path reaches memory through the arbiter port of [P.15](sec_ai_p#p15), not through the blitter, so whichever engine wins — sheet T's operations or sheet U's channels — this block is unchanged. It is worth saying because almost everything else about Neon is currently conditional on that question.

## The part — the PCM5102A, and its strapping is half the specification.

| Pin | Strap | What it decides |
|---|---|---|
| `FLT` | GND | Normal latency filter rather than low latency |
| `DEMP` | GND | De-emphasis off — it is a 44.1 kHz-only legacy filter and would colour everything else |
| `FMT` | GND | **I2S format** — data delayed one `BCK` after the `LRCK` edge, MSB first |
| `XSMT` | 3V3 through an RC | Soft un-mute, delayed past the rail's edge. **The delay is the anti-pop** |
| `SCK` | **GND** | **The part runs its own internal PLL off `BCK`** and needs no master clock |

- W.4 — **`SCK` to GND is the decision the pin count rests on.** With it grounded the PCM5102A synthesises its own internal clocks from `BCK` alone, so Neon owes the DAC three driven signals — `BCK`, `LRCK`, `DIN` — and not four ([P.09](sec_ai_p#p09)). [Sheet T](sec_ai_t)'s pin budget still carries the four-pin figure with a conditional beside it; [sheet H](sec_ai_h) has already grounded the pin, so the condition is met and the budget should say three ([W.38](sec_ai_w#w38)).
- W.5 — **It also decides what the sample rate *is*, which is the part nobody has written down.** There is no master clock against which `BCK` is a ratio; the DAC's PLL locks to `BCK`, and the sample rate is therefore **exactly `BCK` divided by the frame length**, whatever Neon happens to emit. Software cannot ask for 44.1 kHz. It gets whatever the divider produces, and the machine's only honest move is to tell it what that is ([W.29](sec_ai_w#w29)).
- W.6 — **The frame is 32 `BCK` per stereo pair — 32fs — and that is a specification, not an economy.** Two 16-bit slots is exactly the payload with no padding bits, and [W.11](sec_ai_w#w11) shows it is also the only ratio from which an integer divider lands anywhere near a standard rate. The part's PLL supports 32fs, 48fs and 64fs; 64fs is the more common choice elsewhere and is the wrong one here.
  NOTE: **Checked against the datasheet, and it holds with one condition attached that this design already meets.** 32fs is the *minimum* ratio the part accepts, valid in PLL mode at every rate from 32 kHz upward — and it is legal **only with 16-bit audio data**: 16-bit takes 32, 48 or 64fs, 24-bit takes 48 or 64, 32-bit takes 64 alone. The mixer emits 16-bit ([W.16](sec_ai_w#w16), [W.27](sec_ai_w#w27)), so the condition is satisfied by construction rather than by care — **but it means the frame length and the sample width are now coupled**, and widening the output later would cost the pin [W.4](sec_ai_w#w4) saved.
  NOTE: 8 kHz supports neither 32fs nor 64fs and 16 kHz supports only 64fs. **Both are unreachable here and neither is wanted**; [W.12](sec_ai_w#w12)'s writable divisor must not be swept into them during bring-up.
- W.7 — `XSMT`'s RC is sized for the rise of 3V3_MAIN, and the part is on that rail with the panel logic, the touch controller and the card ([C.10](sec_ai_c#c10)). Its share of the budget is already counted — 95 mA typical in the 130 mA group of [sheet C](sec_ai_c) — and audio adds nothing to the power path that is not already there.
  NOTE: **The strap should not be a strap at all, and the datasheet is blunter about it than this sheet was: `XSMT` must be a driven pin.** An RC can only delay un-mute past a *rising* edge; it can do nothing about a falling one, which is exactly the power-down click of [W.40](sec_ai_w#w40). **The general rule it is an instance of: no functional strap belongs on a passive-only net** (→ [Q113](sec_ai_q#q113)).
  NOTE: **Which party drives it is the open half, and the answer is probably not the one [W.40](sec_ai_w#w40) proposed.** That item offered a Neon pin driven by `AUD_CTRL.MUTE`, which is the natural place while the machine is running and the wrong place at both edges that matter: at power-up Neon is not configured yet, and at shutdown the switched domain is coming down around it. **That is [T.52](sec_ai_t#t52)'s argument for the backlight, verbatim** — the EC is the only party awake before Neon exists and while the rails are dropping — and it points the same way here, at one GPIO out of the 48 [D1.6](sec_ai_d1#d16) now has spare. `AUD_CTRL.MUTE` then stays a *mixer* mute, which is what software wants between tracks, and `XSMT` becomes a power-sequencing signal owned by whoever owns the sequence.
- W.7a — [[!blocking]] **`BCK` and `LRCK` must be continuous and glitch-free whenever the DAC is powered, including when nothing is playing.** The part re-initialises itself if the relationship between the two is invalid for more than four `LRCK` periods, and forces its outputs to bipolar zero until they resynchronise — so **gating the clocks to save power, or stopping them between tracks, produces a mute of up to a sample period and a re-lock of the internal PLL** every time. The serialiser therefore runs from reset and emits zero PCM when idle; it never stops.
  NOTE: This is not in tension with [W.22](sec_ai_w#w22)'s underrun behaviour, it is the same instinct one level down: **a channel holds its last sample rather than emitting zero because a step to zero is a click, and the serialiser emits zeros rather than nothing because stopping is worse than silence.** It also means [W.2](sec_ai_w#w2)'s "a tone depends on nothing" has a companion obligation — the clocks depend on nothing either, and must survive every mode change, every blit storm and every idle period.
  TEST: scope `BCK` and `LRCK` continuously across a mode switch, a blitter saturation run and ten minutes of silence — no gap, no phase discontinuity, and `LRCK` exactly `BCK`/32 throughout.

## The sample rate does not fall out of the clock, and this is the finding the sheet exists for.

Neon is one clock domain at **103.125 MHz** ([D28](sec_ai_q#d28), [T.8](sec_ai_t#t8)), derived from the board's single 25 MHz oscillator ([B.7](sec_ai_b#b7)) as 25 × 33 / 8. A `BCK` divider off that domain is an integer.

| Target | Frame | Exact divisor | Nearest integer | Rate obtained | Error |
|---|---|---|---|---|---|
| 44.1 kHz | 32fs | 73.078 | **73** | **44,146.0 Hz** | **+0.104 %** |
| 48 kHz | 32fs | 67.139 | 67 | 48,099.3 Hz | +0.207 % |
| 44.1 kHz | 64fs | 36.539 | 36 · 37 | 44,758.7 · 43,549.4 Hz | +1.49 % · −1.25 % |
| 48 kHz | 64fs | 33.569 | 33 · 34 | 48,828.1 · 47,391.8 Hz | +1.73 % · −1.27 % |

- W.8 — **At 64fs nothing lands within 1.2 %, and at 32fs both standard rates land within 0.21 %.** That is the whole argument for [W.6](sec_ai_w#w6), and it is a property of 103.125 MHz rather than of the audio design — the divider is small, so the quantisation is coarse, and halving the frame halves the divisor's granularity in the only direction that matters.
- W.9 — **Specified: divisor 73, `BCK` = 1.412671 MHz, fs = 44,146.0 Hz.** The error is **+1.8 cents** of pitch and **3.75 seconds per hour** of drift. The first is below the threshold at which a trained listener identifies a mistuning in isolation; the second matters only against an external time reference, and this machine has none — there is no video playback to sync to and no capture path to sync with.
- W.10 — **The frame is exactly 2,336 clocks, by construction** — 32 × 73 — so every downstream number on this sheet is an integer and the mixer's schedule ([W.26](sec_ai_w#w26)) has no ragged edge to design around. Choosing the divisor first and reporting the rate afterwards is what buys that; choosing the rate first would have bought a fractional divider and a jittered `BCK`.
- W.11 — [[!blocking]] **Exact 44.1 or 48 kHz is unreachable on this board by any PLL configuration, and that closes half of [Q54](sec_ai_q#q54).** The iCE40 PLL is integer-only — `fout = fin × (DIVF+1) / ((DIVR+1) × 2^DIVQ)` — so from 25 MHz the achievable denominators are `(DIVR+1) ≤ 16` times a power of two. **44.1 kHz needs a factor of 441 = 3² × 7²** in the denominator and **48 kHz needs 3,125 = 5⁵**; neither is expressible in that form at any setting. The second PLL cannot produce an exact audio clock, so **audio has no claim on it** — not a weaker claim than VGA's, as [T.9](sec_ai_t#t9) says, but none at all. Q54 reduces to the question it should always have been: whether VGA is worth its two pins.
  NOTE: **The two exact routes both cost a part, and both are refused here.** A second oscillator at 11.2896 or 24.576 MHz gives 44.1 or 48 kHz exactly and breaks [B.7](sec_ai_b#b7)'s one-oscillator star, which exists so that no cross-chip skew has to be reasoned about. A fractional NCO gives an exact *average* rate with one 103.125 MHz period — 9.7 ns — of cycle-to-cycle jitter on `BCK`, fed to a PLL whose datasheet expects a clean one. **Neither is worth 1.8 cents**, and the decision should be recorded rather than rediscovered (→ [Q77](sec_ai_q#q77)).
- W.12 — **`AUD_DIV` is writable anyway**, defaulting to 73. It costs eight flip-flops and buys three things: a bring-up sweep that finds the DAC's actual lock range instead of trusting it, a path to 48 kHz for material that arrives at 48, and the ability to slew the rate deliberately if a resampler is ever written. `AUD_RATE` tracks it, so software is never told a rate the hardware is not producing.
- W.13 — **Resampling is a software problem and this sheet does not solve it.** Content authored at exactly 44.1 kHz plays 0.104 % sharp, which is correct behaviour for a machine with one clock: the alternative is an interpolator in the mixer, and an interpolator needs a multiplier per channel per sample rather than the one time-multiplexed unit of [W.26](sec_ai_w#w26). **Tracker and sample playback are unaffected in principle** — a sample's pitch is set by its own step rate, not by fs — and that is what this machine is actually for.

## What plays — four channels, and the count is derived rather than chosen.

- W.14 — **Four, because that is what [T.13](sec_ai_t#t13)'s 0.7 MB/s already is.** One stereo stream at 16 bits and this sheet's rate is 44,146 × 4 = **176,584 B/s**; four of them is **706 KB/s**, which is the figure sheet T has carried since REV B with nothing behind it. The number was right and its derivation was missing. [U.14](sec_ai_u#u14)'s "< 1 MB/s plus mixing reads" is the same figure with the mixing reads counted twice — in hardware the mixed result never goes to memory at all.
- W.15 — **Mixing in hardware costs reads only, and that is not a small distinction.** A software mixer reads N streams and writes one buffer; this one reads N streams and writes nothing, because the accumulator's output goes straight into the serialiser. **There is no audio write traffic anywhere in the machine.**
- W.16 — Each channel is independently **16-bit or 8-bit**, **mono or stereo**. A mono channel costs half the bandwidth and is the common case — a tracker's voices, a UI click, a game's effects are all mono, and stereo is for streamed music. Four stereo 16-bit channels is therefore the worst case rather than the expected one; four mono 16-bit channels is 353 KB/s.
- W.17 — **Per-channel `VOL_L` and `VOL_R`, 8-bit linear, and pan is what falls out.** Two volumes rather than a volume and a pan costs one more register and no more hardware — the multiplier runs twice per frame per channel either way — and it expresses hard-panned, centred and anything between without a pan law having to be agreed on.
- W.18 — **Why not eight channels, and why not one.** Eight doubles the ring generators and the multiplier's schedule, still fits the bandwidth trivially, and does not fit the block RAM ([W.36](sec_ai_w#w36)) — which is the constrained resource on this device and the subject of a blocking question already. One channel puts mixing back on the 65816 and is excluded by [W.1](sec_ai_w#w1). Four is the number at which a GUI's system sounds, a game's music and two effect voices coexist without the kernel having to steal one.

## The DMA engine — one ring per channel, and the underrun behaviour is a decision.

- W.19 — Each channel is a **ring buffer in Neon's SDRAM**: a 24-bit base, 16-byte aligned, and a length in 16-byte units, giving 16 B to 1 MB. Buffers live in **bank 3** with the font atlas and the icon sheets ([D38](sec_ai_q#d38), [U.16](sec_ai_u#u16)) — the bank that carries everything read at low rate, which is exactly what audio is, and which keeps the framebuffer banks free for the invariant of [U.17](sec_ai_u#u17).
- W.20 — **`LOOP` distinguishes the two things a ring is used for.** Set, the pointer wraps and the channel plays until stopped — streaming refilled by software, or a looped instrument. Clear, the channel plays `LEN` once, stops, clears its own `EN` and raises an end interrupt — a fire-and-forget sound effect, which is what a GUI and a game mostly want and which costs no further CPU attention at all.
- W.21 — **The fetch engine requests 8 words, 16 bytes, and never less.** That is [U.18](sec_ai_u#u18)'s minimum burst, obeyed here for the same reason: shorter fails to amortise `tRCD` and the arbitration handover. At one stereo channel's rate a 16 B burst covers four frames — 90.6 µs — so four active channels average **one burst per sample frame**, about 28 of the frame's 2,336 clocks.
- W.22 — **On underrun the channel holds its last sample and sets a sticky `UNDERRUN` bit.** The alternative — emitting zero — is worse and obviously so: a jump from wherever the waveform was to zero is a step discontinuity, which is a click, and a stream that stutters would click twice per gap. Holding is a DC offset for the duration, which is inaudible for the milliseconds an underrun lasts and audible only as the silence it already is. **The flag is what matters**: an underrun that cannot be observed is a bug report that says "it crackles sometimes."
- W.23 — **`AUD_POS` reports the read pointer, latched atomically.** Writing `AUD_POS_SEL` captures the selected channel's 24-bit pointer into three registers that the CPU then reads at its leisure — the same latch-then-read discipline [S.16](sec_ai_s#s16) uses for telemetry, and for the same reason: a 24-bit value moving 44,146 times a second, read a byte at a time by an 8-bit CPU, otherwise produces pointers that were never real.
- W.24 — **The refill interrupt fires at the half-way point and at the wrap**, which gives software a whole half-buffer of slack. A 4 KB stereo ring is 23.2 ms of audio, so the interrupt arrives every **11.6 ms** — a comfortable multiple of the 100 Hz scheduler tick ([N.3](sec_ai_n#n3)), and a refill that misses one deadline still has the other half of the ring behind it. [P.11](sec_ai_p#p11)'s 128 KB of audio buffers on the prototype is four channels of 32 KB, which is eight times this and never a constraint.

## The mixer — one serial multiplier, time-multiplexed, and the schedule is not tight.

- W.25 — Per frame: for each enabled channel, fetch its next sample from the channel FIFO, sign-extend 8-bit sources, multiply by `VOL_L` and by `VOL_R`, and accumulate into two 24-bit sums. Then **saturate** to 16 bits and hand both to the serialiser.
- W.26 — **Eight multiplies per frame against 2,336 clocks, so one shift-add multiplier does the whole job.** A 16 × 8 serial multiplier is eight cycles; eight of them is 64 clocks, **2.7 % of the frame**. Building four parallel multipliers would cost roughly 600 LUT and buy nothing — this is the same economy the coordinate multiplier of [T.42](sec_ai_t#t42) applies to the command processor, one device over.
- W.27 — **The accumulator saturates rather than wraps, and this is not a detail.** Four channels at full volume sum to four times full scale; a wrapping accumulator turns that into a sign flip, which is full-amplitude noise — the loudest possible output at the exact moment the material is loudest. Saturation is a comparator and a mux per rail. **Wrapping is the single easiest way to destroy a speaker or a listener on this machine**, and it must be in the RTL from the first line rather than added after the first time somebody hears it.

## Register map — Neon's page at `$FF:8000`, offsets `$80`–`$AF`, privileged and unmappable ([T.21](sec_ai_t#t21)).

Channel *n* occupies eight bytes at `$80 + 8n`, for *n* = 0…3.

| Offset | Name | Access | Description |
|---|---|---|---|
| `+0`–`+2` | `AUDn_BASE` | W | Ring base, 24-bit, 16-byte aligned; low 4 bits ignored |
| `+3`·`+4` | `AUDn_LEN` | W | Ring length in 16-byte units, 1–65535 |
| `+5`·`+6` | `AUDn_VOL_L` · `AUDn_VOL_R` | R/W | Linear volume per output rail, 0–255 |
| `+7` | `AUDn_CTRL` | R/W | b0 `EN` · b1 `LOOP` · b2 `FMT8` · b3 `STEREO` · b4 `IRQ_HALF` · b5 `IRQ_END` |
| `$A0` | `AUD_CTRL` | R/W | b0 `MASTER_EN` · b1 `MUTE` — drives `XSMT` low · b2 `RESET`, clears every pointer and the FIFO |
| `$A1` | `AUD_STATUS` | R | b3:0 channel active · b4 `UNDERRUN`, sticky, cleared on read · b5 `XSMT` state as driven |
| `$A2` | `AUD_IRQ_STATUS` | R/W1C | b3:0 half-buffer per channel · b7:4 wrap or end per channel |
| `$A3` | `AUD_IRQ_EN` | R/W | Same bits |
| `$A4`·`$A5` | `AUD_RATE` | R | **Actual** sample rate in Hz. `$AC72` = 44,146 at the default divisor |
| `$A6` | `AUD_DIV` | R/W | `BCK` divisor from the 103.125 MHz domain. Default `$49` = 73 |
| `$A7` | `AUD_POS_SEL` | W | Channel select; **the write latches that channel's pointer** into `AUD_POS` |
| `$A8`–`$AA` | `AUD_POS` | R | Latched 24-bit read pointer |
| `$AB` | `AUD_TEST` | R/W | b0 enable · b3:1 tone divisor · b7:4 amplitude. Square wave into the serialiser, no memory touched |
| `$AC`–`$AF` | — | — | Reserved |

- W.28 — **`$80`–`$AF` collides with nothing.** [Sheet T](sec_ai_t)'s map ends at `$68` with `$70`–`$73` free, and [sheet U](sec_ai_u)'s proposed blitter map occupies `$00`–`$3F` of the same page under either outcome of [Q58](sec_ai_q#q58). Audio sits above both with 32 bytes of room to grow, which is what the page was left with space for.
- W.29 — **`AUD_RATE` is read-only and exists so that no piece of software ever has to assume a rate.** It is the whole software consequence of [W.5](sec_ai_w#w5): a mixer, a tracker replayer or a WAV player computes its step rate from what the register says rather than from a constant in a header, and the day `AUD_DIV` changes, nothing downstream is wrong. **A machine that knows its own sample rate costs two bytes of register file.**
- W.30 — `AUDn_BASE` and `AUDn_LEN` **latch on the write to their highest byte**, so a partially written pointer can never be acted on — the same rule as [T.48](sec_ai_t#t48) and [U.24](sec_ai_u#u24), and it is worth being identical across every block in Neon rather than nearly identical.

## Interrupts — a fourth source on a wire that carries three.

- W.31 — [Sheet T](sec_ai_t)'s interrupt table has bits 0, 1 and 2 — vblank, raster compare, `SIGNAL` — aggregated into the single `NEON_IRQ` line into Helium's PIC ([T.47](sec_ai_t#t47)). **Audio takes bit 3**, and `AUD_IRQ_STATUS` demultiplexes which of the eight conditions raised it. No new pin, no new wire, and the kernel's single interrupt entry path is untouched.
- W.32 — **The rate is per streaming channel, and only streamed channels have one.** With a 4 KB ring one channel interrupts **86 times a second**; four of them is 345, against a 100 Hz scheduler tick. A channel playing a **resident sample** — one-shot or looped, already in SDRAM — interrupts once at the end or never, because there is nothing to refill. **The interrupt rate is a property of how many streams are running, not of how many channels exist**, and the ring length trades it away directly: 32 KB gives 10.8 a second.
  NOTE: **Bring-up needs no interrupt at all.** A driver can poll `AUD_POS` against the half-way mark, which is what stages A0 and A1 do — exactly as [T.47](sec_ai_t#t47)'s note describes for vblank, and for the same reason: the gateware arrives before the PIC does.

## Arbitration and bandwidth — the cheapest client in the machine.

| Client | Priority ([D39](sec_ai_q#d39)) | Demand | Slack against the arbiter's 223 ns worst case |
|---|---|---|---|
| Scanout FIFO | 1 | 36.8 MB/s | 5.8 words of a 512-word FIFO ([U.27](sec_ai_u#u27)) |
| **Audio DMA** | **2** | **0.7 MB/s worst case** | **0.72 ms of buffered audio — about 3,200×** |
| CPU aperture | 3 | 1.6 MB/s | Stalls PHI2 ([D37](sec_ai_q#d37)) |
| Blitter | 4 | the rest | Best effort |

- W.33 — **Audio's real-time claim is real but enormously over-served.** A 128-byte per-channel FIFO holds 32 stereo frames — **0.72 ms** — against an arbiter whose proven worst case is 223 ns. [U.28](sec_ai_u#u28) says the scanout guarantee survives audio's demand doubling; the inverse is worth stating too, which is that audio's guarantee survives essentially anything the rest of the machine can do to it.
- W.34 — **Which means audio is the cheapest thing in the arbiter to demote, if the CPU's latency is ever measured as a problem.** Its placement above the CPU costs a CPU access at most one extra burst — about 78 ns — and buys jitter immunity it does not need. **The recommendation is to leave [D39](sec_ai_q#d39) exactly as it stands**: 78 ns is not worth an argument, and a fixed priority order that has been reasoned through once is worth more than a marginally better one that has not. It is recorded here so that if that latency is ever contested, the cheapest concession is already identified.

## Resource cost — and it gives a block back to [Q61](sec_ai_q#q61).

| Block | LUT4 | EBR |
|---|---|---|
| I2S serialiser, `BCK`/`LRCK` divider, test tone | 60 | — |
| Ring address generators, four channels | 140 | — |
| Fetch engine and memory port | 90 | — |
| Channel FIFOs, 128 B each | — | **1** |
| Serial multiplier, time-multiplexed | 60 | — |
| Accumulator with saturation | 50 | — |
| Register file, IRQ logic, `AUD_POS` latch | 100 | — |
| **Total** | **~500** | **1** |

- W.35 — **[T.50](sec_ai_t#t50) reserves 800 LUT for audio and this is the first time that reservation has had anything behind it.** ~500 returns roughly 300 LUT to Neon's margin, which moves the device from ~77 % to about 73 %. That is not slack to spend — it is the difference between a routable design and an argument with the place-and-route tool, and [T.45](sec_ai_t#t45) is on record that the whole point of removing the sprite engine was to fit audio at all.
- W.36 — **One block RAM, not two, and that is a real relief on a blocking question.** iCE40 EBR is 4 kbit — 512 B — and four channel FIFOs of 128 B fit one block exactly, written on port A by the fetch engine and read on port B by the mixer. [U.36](sec_ai_u#u36) budgeted **2 blocks for audio** with no channel count behind the figure; [Q61](sec_ai_q#q61) then counted those two against a design already at 32 of 32. **This returns one of them.** The question does not close — it wants 33 or 34 blocks against 32, and the 128-glyph font of [Q45](sec_ai_q#q45) is still the relief that decides it — but it is one block less bad than it was, and the figure it replaces was an estimate rather than a design.
- W.37 — **Eight channels would want a second block and are refused on exactly that ground** ([W.18](sec_ai_w#w18)). It is the clearest case on the whole device of block RAM, not logic and not bandwidth, deciding a feature — which is what [T.3](sec_ai_t#t3) claims generally and this sheet demonstrates specifically.

## Pins — three, and sheet T's budget closes with one more than it claims.

- W.38 — `BCK`, `LRCK`, `DIN`. [Sheet T](sec_ai_t)'s pin budget lists **4** for I2S with "three if `SCK` is grounded" beside it, and [sheet H](sec_ai_h) grounds `SCK`; the condition is met and has been since the strapping was written down, so **Neon's total is 99 of ~107, not 100**. `XSMT` is not a fourth pin **provided it is the EC that drives it** ([W.7](sec_ai_w#w7)); the count only rises to 100 if the pin is taken from Neon instead, which is the reason the ownership question in [W.40](sec_ai_w#w40) is a pin-budget question as well as an audio one.

## The analogue side — the jack now, the speaker later, and a click nobody has budgeted for.

- W.39 — **Output is a 3.5 mm jack today.** [P.21](sec_ai_p#p21) already places a **PAM8302 and a pair of speaker pads, unpopulated, behind a jumper** on schematic sheet 1.5, against the laptop build at [L4](sec_ai_p#l4) — a square centimetre now against a respin later. This sheet owns that footprint: the DAC's line output feeds the amplifier input, the jumper selects jack or amplifier, and nothing about the gateware changes when it is populated.
- W.40 — [[open]] **Shutdown has no mute step, and it should.** The RC on `XSMT` exists because a rail edge through an un-muted DAC is a click in the headphones ([sheet H](sec_ai_h)) — and [S.13](sec_ai_s#s13)'s shutdown sequence takes the machine all the way to `POWER_CTRL` with nothing anywhere muting the output first. The power-up click was designed out and **the power-down click was never considered**, which is the more audible of the two because the listener is wearing the headphones by then. The fix is two things: `XSMT` driven from `AUD_CTRL.MUTE` rather than strapped, at the cost of one Neon pin, and **a mute step inserted into [S.13](sec_ai_s#s13) between stopping the window server and flushing the filesystem** (→ [Q76](sec_ai_q#q76)).
  NOTE: The ordering matters and is not obvious. Muting must happen **before** the switched domain begins to come down and **after** the last sound the session wants to make — which is exactly where the window server stops, because a shutdown chime, if there ever is one, is the window server's to play. On the EC's side nothing changes: it already sequences the rails and already tri-states its boundary signals ([C.13](sec_ai_c#c13)).
  NOTE: **Half of it is answered and the half that remains is ownership.** The RC goes either way — [W.7](sec_ai_w#w7) — and if the EC drives `XSMT` rather than Neon, the mute step is a GPIO write inside a sequence [S.13](sec_ai_s#s13) already runs, needing no new ordering, no Neon pin, and nothing from a subsystem that may already be unpowered by then. The alternative costs a Neon pin and inherits Neon's power state, which is the thing being muted against.
- W.41 — **Headphone detection is not specified and is not wanted for v1.** The jack's switch contact would cost a pin and an EC input to route the output between jack and speaker automatically; the jumper does it manually until a laptop exists, and by then the choice belongs with the rest of the EC's expander I/O rather than with Neon.

## `/dev/audio` — and it is the one bulk path where the aperture is the right answer.

- W.42 — The driver implements [J.4](sec_ai_j#j4)'s five functions and **adds nothing to the dispatch table**, for the same reason `/dev/power` does not ([S.11](sec_ai_s#s11)): `open` allocates a channel, `write` copies PCM into that channel's ring, `read` returns status, `ioctl` carries volume, rate query, loop and stop, `irq_handler` services the half-buffer interrupt.
- W.43 — **`write()` through the aperture is affordable here, and this is the exact inverse of the framebuffer argument.** [J.5](sec_ai_j#j5) forbids per-pixel `write()` because a process walking the framebuffer runs at the aperture's ~1.3 MB/s while the blitter beside it runs at ~100. Audio's entire worst-case demand is **0.7 MB/s, about half the aperture** — and one stereo stream is 176 KB/s, **14 %**. There is no faster path to compete with, because the alternative is the CPU generating samples anyway. **The rule was never "the aperture is slow"; it was "the aperture is slow compared to the engine beside it", and for audio there is no engine beside it.**
  NOTE: **The worst case is nevertheless expensive, and it is worth knowing which case it is.** Refilling four streamed channels means copying 706 KB/s through the aperture — **about 54 % of the CPU**, because a refill copies every byte it plays. **Four simultaneous streams is not a shape anybody builds**: the normal one is a single streamed channel for music at 14 % and three resident sample channels at nothing, because a sample loaded once and triggered by DMA costs no refill traffic at all ([W.20](sec_ai_w#w20)). A larger ring cuts the interrupt rate and **not** the copy, which is irreducible for streamed data — the only way to spend less is to stream less.
  NOTE: Bulk sample assets take the other path when they are large: the EC's service port loads from microSD into VRAM at ~2.5 MB/s with no CPU involvement ([T.46](sec_ai_t#t46)), which is the same trick as icon and font upload and costs the 65816 nothing.
- W.44 — [[open]] **Four channels are a resource, so somebody has to allocate them.** The recommendation is the simplest one that does not surprise anybody: **channel 3 is reserved for the session's system sounds**, channels 0–2 are allocated to `open` in order, and the fourth caller gets `ENODEV` rather than being silently mixed into somebody else's channel. A software mixer behind a virtual channel is the general answer and is excluded by [W.1](sec_ai_w#w1) — there are no cycles for it — so the honest interface is a finite one that says so (→ [Q78](sec_ai_q#q78)).
- W.45 — **The window server plays the system sounds, not the kernel** — the same split as [Q62](sec_ai_q#q62) draws for the compositor. A click, an alert and an error are session policy; the kernel owns the device node and the ring, and the server owns which sound and when, reaching it through the ordinary `/dev/audio` contract with no privileged path and no new mechanism ([V.28](sec_ai_v#v28)).

## Staging — A0 to A4, and the first of them belongs three stages earlier than the build currently puts it.

| Stage | Content | Acceptance | Board stage |
|---|---|---|---|
| **A0** | I2S serialiser, divider, `AUD_TEST` | A tone at the jack — no SDRAM, no CPU, no Helium | **[E1.7](sec_ai_p#e17)** |
| **A1** | One channel, ring DMA, arbiter port | Continuous playback from SDRAM, no underrun | E5 · [P5.a](sec_ai_p#p5a) |
| **A2** | Four channels, volumes, mixer, saturation | Four streams mixed with no clipping | E8 |
| **A3** | Interrupts, `/dev/audio`, IRQ-driven refill | A user program makes sound ([P5.l](sec_ai_p#p5l)) | E8 · G-series |
| **A4** | One-shot and loop modes, system-sound reservation | GUI sounds under the window server | + |

- W.46 — **A0 needs nothing but the bitstream, three pins and the DAC**, which is the same claim [T.64](sec_ai_t#t64) makes for Mode 0 and it lands in the same place. [Sheet P](sec_ai_p) currently has audio appearing at **E5**, as "tone over I2S" bundled with the whole of video; it belongs at **[E1.7](sec_ai_p#e17)** beside the console, and for the same reason — a bitstream that produces a clean 1 kHz tone has demonstrated the PLL, the divider, three I/O pins, the DAC's strapping, its internal PLL lock, the RC on `XSMT` and the jack, in one shot and before any of it can be blamed on the SDRAM controller. **The pop, if there is one, is heard at E1.7 rather than at E5**, which is the difference between a strapping fix and a respin.
- W.47 — Each stage is a usable machine and nothing is stranded if work stops at one of them, which is [T.63](sec_ai_t#t63)'s property applied to a much smaller block: A1 alone plays music, A2 alone runs a tracker, and A3 is the first stage at which a user process is involved at all.

## Verification — and the first four need no CPU.

- [ ] W.48 — **A 1 kHz tone from `AUD_TEST`** is audible at the jack with the 65816 unpopulated, driven entirely from the EC's service port.
- [ ] W.49 — **`BCK` measures 1.4127 MHz ±100 ppm** on a scope, and `LRCK` is exactly `BCK`/32 with the correct phase for I2S.
- [ ] W.50 — **No click at power-up.** Scope `XSMT` against 3V3_MAIN and confirm the RC holds the DAC muted past the rail's edge.
- [ ] W.51 — **The DAC's internal PLL locks and stays locked** across the full writable range of `AUD_DIV`, and the range at which it does not is recorded rather than assumed.
- [ ] W.52 — **Continuous single-channel playback for ten minutes** from SDRAM with `UNDERRUN` never setting.
- [ ] W.53 — **A deliberately induced underrun** — refill interrupt masked — holds the last sample, sets the flag, and recovers cleanly when refill resumes.
- [ ] W.54 — **Four channels at full volume on correlated material saturate rather than wrap**, verified by ear and on a scope: the output clips flat, and no sign inversion appears anywhere.
- [ ] W.55 — **`AUD_POS` never reports a pointer outside its ring**, sampled continuously across a wrap, which is the test that the latch of [W.23](sec_ai_w#w23) actually latches.
- [ ] W.56 — **Audio survives a full-screen blit storm**: playback with the blitter saturating memory shows no underrun and no audible artefact, which is the arbiter claim of [W.33](sec_ai_w#w33) as an experiment rather than a calculation.

## What this sheet changes in other sheets, recorded so the edits are not lost.

| Sheet | Change |
|---|---|
| [T.9](sec_ai_t#t9) | Audio has **no** claim on the second PLL — not a weaker one. No PLL setting reaches 44.1 or 48 kHz from 25 MHz ([W.11](sec_ai_w#w11)) |
| [T.47](sec_ai_t#t47) | The interrupt table gains **bit 3, audio** ([W.31](sec_ai_w#w31)) |
| [T.51](sec_ai_t#t51) | Pin budget: I2S is **3**, total **99 of ~107**, not 100 ([W.38](sec_ai_w#w38)) |
| [T.50](sec_ai_t#t50) | Audio's 800 LUT reservation measures at **~500** ([W.35](sec_ai_w#w35)) |
| [U.36](sec_ai_u#u36) | Audio is **1 EBR block, not 2**, which improves [Q61](sec_ai_q#q61) by one block ([W.36](sec_ai_w#w36)) |
| [U.14](sec_ai_u#u14) | "< 1 MB/s plus mixing reads" double-counts: hardware mixing writes nothing ([W.15](sec_ai_w#w15)) |
| [S.13](sec_ai_s#s13) | The shutdown sequence needs a **mute step** before the rails drop ([W.40](sec_ai_w#w40), [Q76](sec_ai_q#q76)) |
| [P.05](sec_ai_p#p05) · [E5](sec_ai_p#e5) | Stage **A0 moves from E5 to [E1.7](sec_ai_p#e17)** ([W.46](sec_ai_w#w46)) |
| [Q54](sec_ai_q#q54) | Reduces to "is VGA worth two pins", with audio's half of it closed ([W.11](sec_ai_w#w11)) |
