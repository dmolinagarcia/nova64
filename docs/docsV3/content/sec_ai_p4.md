# Beyond the board · the laptop L1–L8
> what turns a working board into a machine

One series that sits outside the three phases of [sheet P1](sec_ai_p1), and outside them for a reason of its own. The **L-series** runs after [E8](sec_ai_p3#e8) has closed and turns a working board into something you pick up, open and close. The filesystem work that used to share this sheet is [sheet Y4](sec_ai_y4), which needs no board at all.

It is not on the critical path for either milestone, which is exactly why it is listed here rather than folded into the stages it follows and allowed to delay them.

## Beyond E8 · L1–L8 — the portable machine

[E8](sec_ai_p3#e8) closes with a working noVa64 on a board that runs on battery and shows a windowed desktop. What follows turns that into something you pick up, open and close, and none of it is on the critical path for either milestone — which is exactly why it is listed separately rather than folded into E8 and allowed to delay it.

- [ ] L1 — **Internal keyboard matrix**, designed and scanned by the EC firmware, delivered through the existing input path of [V.30](sec_ai_v#v30) so that nothing above the HID driver knows the difference. A built-in keyboard replaces USB HID.
  TEST: every key and every two-key combination scanned without ghosting, with the HID driver unmodified above it.
  NOTE: [D1.6](sec_ai_d1#d16) moved a matrix *off* the EC's pins onto USB HID so its budget would close; this stage brings one back, so where it scans is that budget reopened.
- [ ] L2 — **Integrated pointing device** selected and driven into `wserver` through the same queue — pointer input with no external peripheral attached.
  TEST: the pointer crosses the full screen with no external peripheral attached, through the same queue and with no new event type.
- [ ] L3 — **Software power management** — rail gating, sleep and wake, battery status surfaced in the GUI through the `$FF` power block of [sheet S](sec_ai_s).
  TEST: sleep and wake with the display, the card and the audio path all restored, and the battery indicator agreeing with the gauge after the resume.
  NOTE: This is the stage where [Q73](sec_ai_q#q73) stops being theoretical. Everything resting on the power block had no prototype hardware at all ([P2.02](sec_ai_p2#p202)), so the battery indicator, the shutdown dialogue and the stale-telemetry behaviour get their first real exercise between [E8](sec_ai_p3#e8) and here.
- [ ] L4 — **Mechanical CAD** — lid and hinge, mainboard mounting, battery bay, port cutouts, keyboard tray. A complete enclosure model before anything is printed.
  TEST: the model checked against the assembled board's measured outline, with every connector reachable and every fastener placed before anything is printed.
- [ ] L5 — **Printed enclosure prototype** assembled, with the fit and cable-routing revisions applied. Everything physically fits and the lid closes.
  TEST: the lid closes, every port is usable with the case shut, and no cable is under strain in either lid position.
  NOTE: The revisions this stage produces are the ones CAD cannot predict — cable bend radius, connector access with a lid at 100°, and where a hand actually holds the machine.
- [ ] L6 — **Thermal profiling and battery-life characterisation** under sustained GUI load, inside the enclosure rather than on a bench.
  TEST: sustained GUI load to thermal equilibrium inside the closed case, with the battery figure taken on the same run rather than a separate one.
  NOTE: Inside the case is the only measurement that counts. A board that is comfortable in open air and a board in a sealed printed shell are different thermal problems, and the second one is the product.
- [ ] L7 — **REV B**, correcting the errata accumulated from [E1.4](sec_ai_p3#e14) onward, and final assembly into the enclosure.
  TEST: the errata list closed item by item — each one either fixed in REV B or recorded as accepted, with nothing left unclassified.
- [ ] L8 — **Release** — schematics, gateware, kernel, SDK, disk image, build instructions, user manual.
  TEST: someone who is not you builds it from what is published.
  NOTE: That test is also this document's: [A2.7](sc_a2#a27) claims nothing here is written anywhere else, and a stranger building the machine is how that gets checked.

!!! PORTABLE — a noVa64 you can pick up, open and use.
