# How other multi-material systems show filament

Condensed on 2026-09-29 from a Gemini deep-research survey of the UI conventions of Bambu AMS, Prusa MMU3, Happy Hare, AFC/Box Turtle, Snapmaker U1, Anycubic ACE, Creality CFS and Qidi Box. Ten of its claims were spot-checked: eight held and two are unverified (marked below). Everything else is the survey's word, and the report itself is not kept.

## What most of them share
- **Tool numbers in Klipper systems.** Prusa, Happy Hare, AFC and Qidi all name the virtual extruder `T0…Tn`. Bambu labels units and slots instead (`A1`, `B2`), the same way on the printer, in the slicer and in the app ([Bambu wiki](https://wiki.bambulab.com/en/ams/manual/ams-function-introduction)). Our tool-first labels follow the Klipper convention, and we have no unit letters because OpenAMS configs have no unit-to-letter mapping.
- **Words for a position:** slot (Bambu, Anycubic, Creality, Qidi), gate (Happy Hare) or lane (AFC).
- **Spares:** every system switches to another spool at runout, and halts and asks when there is no match. It is called auto-refill, backup, SpoolJoin or Endless Spool.
- **Remaining filament:** commercial units estimate it from RFID. Klipper systems show grams from Spoolman. An unknown amount is shown as unknown, not guessed.
- **Errors:** a plain sentence, often a code (Bambu's HMS codes, Snapmaker's), and a recovery action (Retry, Recover).
- **Load steps:** named phases (Happy Hare: gate, Bowden, home to extruder, tip forming; Snapmaker: heat, feed, purge).
- **Verbs:** Load, Unload and Purge everywhere; Eject, Pre-load, Recover and Re-read are common.

## Where they differ
- **How much they expose:** Bambu and Creality hide the routing (a slot holds a color). Happy Hare and AFC expose gates, lanes and a tool-to-gate map.
- **How a spare is chosen:**
  - an exact match on brand, type, color and nozzle temperature (Bambu; verified);
  - order of position (Prusa SpoolJoin; unverified);
  - an explicit slot (Qidi "Backup Slot"; unverified);
  - user-defined groups (Happy Hare Endless Spool; verified).

  OpenAMS uses groups, and our `∞1`, `∞2` badges make the order visible.

## User complaints, and what they teach us
| Complaint | Source | Lesson |
|---|---|---|
| Bambu auto-refill needs an exact match, so near-identical spools won't pair | Bambu wiki (verified); Reddit (not opened) | Make the spare relationship explicit and visible (our `∞` badges). |
| Bambu auto-refill resumes without a prime tower, leaving blobs | [BambuStudio #7574](https://github.com/bambulab/BambuStudio/issues/7574) (verified) | Firmware and host behavior, not UI. |
| AFC loses spool assignments when Spoolman is late at boot; LANE_MOVE does nothing after an e-stop | [AFC #816](https://github.com/ArmoredTurtle/AFC-Klipper-Add-On/issues/816), [#815](https://github.com/ArmoredTurtle/AFC-Klipper-Add-On/issues/815) (verified) | **Never fail silently** (principle 9). When Spoolman is unreachable, link actions are dimmed with that reason. |
| Happy Hare's Spoolman push fails, and the UI loses color and weight | [Happy Hare #880](https://github.com/moggieuk/Happy-Hare/issues/880) (verified) | Show a missing source as "unknown", never as a wrong value. |
| Snapmaker U1 false-positive error codes need repeated Retry; Anycubic ACE jams on refill | Reddit (not opened) | None recorded. |

## Takeaways for our principles
All of these are already covered:
- the tool-first labels;
- a visible spare order;
- `?` for unknown amounts;
- named load steps;
- errors with a plain sentence and a recovery action;
- acknowledging actions and never failing silently.

Two small additions are open for decisions:
1. **Show the host's fault code** in the details of a fault, small and secondary, so users can search for it and quote it in support.
2. **A runout with no spare left** must say so, and what to do: "T0 ran out; no spare spool is left".
