# Design principles (the grammar of the OpenAMS UI)

These principles are the language that both renderers speak: the ESP32 display (LVGL) and the Mainsail/Fluidd panel (Lit). The owner's review rulings in `UNIFIED_UI.md` §4a and §4b are examples of them. When a case is not covered by a ruling (a five-lane EMU, the spool editor, a new alert kind), derive it from these principles first, and ask only when two principles conflict.

**Sources.** Distilled from Apple's Human Interface Guidelines, taking the principles and their reasoning, not Apple's look:
- *Human Interface Guidelines: The Apple Desktop Interface* (1987), chapter 1, "Philosophy" (`archive.org/details/human-interface-guidelines`);
- the current HIG (2025–26), `developer.apple.com/design/human-interface-guidelines`, cited below by page name.

Everything is paraphrased except the three short quotes. The 1992/1995 Macintosh HIG was not reachable, so nothing here depends on it.

Two further sources, taken from a third-party compilation and kept only where they add to these principles:
- the response-time limits (Nielsen, "Response Times: The 3 Important Limits", 1993, after Miller, 1968);
- the measurable floors of WCAG 2.2 (principle 12).

The 1987 guidelines open with the premise that still holds: *"People aren't trying to use computers—they're trying to get their jobs done."* Our users are trying to print. The UI exists to make the state of their filament obvious and the next action easy, and otherwise to stay out of the way.

---

## 1. Content over chrome

**The principle:** the filament is the content. Everything else (frames, headers, buttons) is chrome, and chrome stays quiet so the content reads first.

**Why:** the UI helps people understand the content and never competes with it. Branding is expressed through restraint, not logos, because screen space belongs to what people came for. (Current HIG: design principles, branding; the iOS 7-era "deference".)

**Here:**
- A tile's color is its spool's color. Chrome is neutral.
- The accent color marks only what is interactive or selected (the loaded spool's outline).
- There are no logos in the panel.
- Inside a host, the host's surfaces and fonts are used (§6 host mode). We are a guest in Mainsail.

## 2. Stable landmarks: state changes content, never geometry

**The principle:** the frame is fixed. Every card, header, row and slot has the same size and position whatever the state. Only what is inside them changes.

**Why:** people cope with a complex system through fixed reference points, and what matters is the *feeling* of stability. Content may change freely as long as the landmarks stay put. (1987: "perceived stability". Current HIG, design principles, flexibility: keep content and controls in consistent, predictable positions.)

**Here:**
- The owner said: "I hate when heights change in layouts".
- Reserved slots for alerts, the message row, the stepper and action buttons.
- Four tiles across at every width; the unit header's fixed height.
- The stepper shows one label at narrow widths rather than truncating.

**Therefore:**
- A state that needs more room gets a place designed for it. It never pushes its neighbors.
- **Loading draws the frame.** While the first data arrives, the panel draws its fixed layout with empty content, never a spinner in its place, so nothing moves when the data lands.
- **An empty state gives the next step.** Connecting, "no OpenAMS configured" and "no units found" are designed states with a clear next step, not a blank panel.
- **A reserved slot has a resting state.** A slot kept for something that comes and goes (the stepper, an alert, an action) shows designed, quiet content while that thing is absent, never a blank gap that reads as something missing or broken. The resting content says something true and useful about the object it belongs to, and it must not repeat a neighbor (principle 4). The owner, 2026-10-02: the status area "looks strange without something there... there should be something default whenever no actions are taking place."

## 3. Position is meaning

**The principle:** a thing sits next to what it is about. Where something appears tells the user what it concerns, before they read it.

**Why:** status placed next to the object it describes lets people understand it without leaving their context. Errors go next to the problem. Grouping by space or enclosure shows what belongs together. (Current HIG: feedback, writing, layout.)

**Here:**
- The alert hierarchy: the host bar is about OpenAMS as a whole, the FPS card's corner about that toolhead, the unit header's end about that unit, and a tag on the ring about that spool.
- The step message under the stepper.
- The loaded tool overlaid on the hotend icon.

**Therefore:** one fact appears at one level. An alert shown in two places claims to be about two things (§4b, one level per alert).

## 4. Say it once

**The principle:** one fact, one place, one message. Do not show the same thing as a tag, a banner and a chip.

**Why:** alerts lose their force when overused, and "a barrage of alert messages" is a sign of a design fault (1987, forgiveness). Screens should not be crowded, and the eye should find the most important thing first (1987, graphic communication). The duplication rule itself is ours; Apple's reasoning supports it.

**The owner's test:** a fact may appear in a second place only if that element "provides additional information and is necessary to understand it". Otherwise it is redundant. "Think of visual space as a limited canvas where less is more, but just enough is absolutely the best."

**Here:**
- The owner said: "We don't need 'runout' then a message about 'T0 ran out', rather a single message".
- A dryer fault lives in the unit alert, not also as a pill.

**Therefore, for a feeder stall in a spool:**
- the tile's tag says **where**, and its details hold the text and the recovery actions;
- the FPS message row says **what** happened, visible without opening anything;
- the unit badge would add neither, so it does not list the stall.

In general, a badge lists only items not already shown elsewhere, or items that carry actions:
- low filament is already shown by the tile's low ring;
- a shortfall with no actions is already said by the message row;
- a load failure keeps its FPS badge, because the badge holds the recovery actions.

## 5. One name for each thing

**The principle:** keep a vocabulary, and use each term the same way everywhere, in the user's language rather than the configuration's internals.

**Why:** consistent terms make a product learnable. Messages in the programmer's vocabulary do not help. (Current HIG, writing: define a vocabulary and use it consistently. 1987, feedback and dialog.)

**Here, the vocabulary:**

| Thing | Name | Never |
|---|---|---|
| A tool (what the slicer calls) | `T0`, `T1`, …; `?` when unassigned | unit letters (`A1`, `B2`) |
| A spool position in a unit | `Spool 1`…`Spool 4`; in a sentence, with the unit: `oams2 Spool 1` | a raw slot id (`oams11`) or a bare `Spool 1` when there are several units |
| A unit | its family ("AMS 2 Pro"), with its config name (`oams2`) under it | the family alone where two units share it |
| An FPS lane / toolhead | its extruder's name ("DragonBurner"), with the FPS id under it | "FPS1" as the title |
| A spare in a group | `∞` on the loaded member; `∞1`, `∞2`, … in order | gaps in the numbering |

**Therefore:**
- Every user-visible string is composed in one place (`src/view/strings.c`), so a term cannot drift between renderers.
- **One icon, one meaning,** in both directions. An icon means one thing everywhere, and one thing always has the same icon. The owner's example: a sun read as "settings", not drying.
- An icon without a text label has an accessible name, and a tooltip on the web.

## 6. Color carries fixed meanings, and never alone

**The principle:** a few colors, each with one meaning that is never redefined: red for errors, yellow for information and caution, the accent for interactive or selected, and the filament's own color as content. Every meaning carried by color is also carried by text, an icon or a shape.

**Why:**
- Color used sparingly keeps its signal.
- Redefining a semantic color breaks trust.
- Red and green are confused by many color-blind people, so color must never be the only channel.
- Contrast must hold in every appearance.

(Current HIG: color, accessibility. 1987 specifications: red stop/error, yellow caution.)

**Here:**
- Red errors and yellow information (§4a).
- Pressure is always neutral: a reading is not a judgment (§4b).
- The error tile has a red border *and* an "Error" tag. Low has the amber arc *and* the percentage.
- An empty bay is hatched, so it cannot be mistaken for a black spool (§4c).

**Therefore:** an alert badge's severity is shown by its icon as well as its color. Contrast is checked in dark, light and both host themes.

## 7. Loudness matches importance

**The principle:** routine status is shown passively; problems earn a mark; only what truly needs the user now may interrupt.

**Why:** "The most effective feedback tends to match the significance of the information to the way it's delivered." (Current HIG: feedback.) Misrepresenting urgency destroys trust (current HIG: managing notifications). Success is expected, so it is confirmed quietly.

**Here, the ladder:**
1. **Passive:** the neutral message row ("Filament loaded", "Loading filament"), tags and rings.
2. **Informational:** a yellow message or badge (low filament, a shortfall for the job).
3. **Error:** a red message or badge (a load failure, a feeder stall).
4. **Interruption:** the printer's own pause. The panel never opens a dialog by itself; it only opens one in answer to the user.

**Therefore:** ask for confirmation only before a drastic or hard-to-reverse action (calibration moves filament), never for routine ones (unload), because common actions that ask every time train people to click through.

## 8. Say what happened, why, and what to do

**The principle:** every problem message names what happened, where, and why when it is known. Its actions are the next steps, labeled with verbs. A disabled action says why it is disabled.

**Why:**
- Be direct, specific and neutral. Do not hide a serious situation in polite phrasing, and do not blame.
- Help solve the problem rather than describe it.
- A lone "OK" is ambiguous; buttons are verbs.
- Say briefly why a command cannot run and what to do instead.

(1987, feedback and dialog and the alert rules. Current HIG: alerts, writing.)

**Here:**
- "The feeder stalled in oams2 Spool 3", with its next steps (retry, resume, clear) as buttons.
- `enabled: false` always carries a `reason` (§2 of the design).

## 9. Show progress honestly

**The principle:** long operations show named steps in a fixed place, advance truthfully, mark the step that failed, and can be stopped when that is safe.

**Why:** a determinate indicator lets people decide whether to wait. Vague words ("loading") hide the task. A stalled indicator must say why. Do not switch between indicator forms, and keep them in a consistent location. Offer cancel when it is safe. (Current HIG: progress indicators, loading. 1987: always provide a way out.)

**Here:** the stepper ("Cut filament → Pull back current filament → Push new filament into extruder → Purge old filament"), the failed step in red with the message under it, and Stop during a load.

**Acknowledge at once, and never send twice.** A command goes to Moonraker, and the printer's state changes seconds later. Within 0.1 s the user must see that the action was taken, so it feels instant. After about 1 s, attention wanders unless the UI shows that work is under way. (Nielsen's three response-time limits.)
- A pressed state shows immediately.
- The action then shows as pending until the view reflects the change, or until a timeout says it did not happen.
- While it is pending, the action cannot be sent again. A double click is one load, not two.

## 10. Everything is visible; shortcuts are extras

**The principle:** every action can be found on screen by looking. Direct targets (click the ring, the tag, the tile) are quick routes to actions that are also listed where people look for them. A problem is visible without opening anything; popovers and dialogs hold details and choices, never the only copy of a warning.

**Why:**
- Recognition beats recall. Anyone unsure must be able to scan the available actions, and shortcuts are an alternative, never a substitute (1987, see-and-point).
- Controls need a visible pressed state, or they feel unresponsive (current HIG: buttons).
- Warnings in a popover are easily missed (current HIG: popovers).
- Only one modal at a time, never nested, always with an obvious way out (current HIG: modality).

**Here:**
- The click targets from §4b: the ring edits, the tag shows details, and the tile loads or unloads.
- The alert badge is always visible; its popover holds the items and their actions.

**Therefore:**
- Each click target has a hover and a pressed state on the web, and the same actions appear in the tile's action list.
- A tile click must be predictable from what is visible: the loaded outline means a click unloads. The verb shows on hover.

**An error is never only a tag.** When a spool is in error and no host fault message covers it, the message row says so: "oams2 Spool 4 reported an error". The tag says where, the row says what, and a tap gives the details and the recovery actions. (From the owner's device review, 2026-09-29.)

**Touch has no hover (the owner's ruling).** A touchscreen is learned by tapping, so we do not add at-rest decorations to mark what is tappable. The web marks it with the cursor and the hover and pressed states. Since a tooltip cannot appear on touch, **tapping a dimmed action shows its reason** (principle 8).

## 11. The user drives; the UI gets out of the way

**The principle:** the user starts actions. The UI offers the next step where the task happens, asks only for what it cannot find out, and keeps settings few.

**Why:**
- People learn by doing and want control (1987, user control).
- Get information from the system instead of asking; offer choices over typing (current HIG: entering data).
- Task-specific options belong where the task happens; the settings area is for rare changes (current HIG: settings).

**Here:**
- The humidity/temperature reading opens drying (§4b).
- RFID and Spoolman fill in the spool, and linking is a choice from a list.
- Load and unload are on the tile itself.

## 12. Every viewer, every host

**The principle:** the panel works for people with different eyes, screens and themes: at phone width, in dark and light, inside Mainsail or Fluidd, and on a small display across the room.

**Why:** perceivable and adaptable. Disabilities are permanent, temporary or situational (glare, distance, one free hand), and designing for them improves the UI for everyone. Stay consistent with the other applications people use. (Current HIG: accessibility, inclusion. 1987, consistency.)

**Here:**
- 360 px is a first-class width.
- The host theme inherits Mainsail's look.
- Milestone B's axe checks.
- The ESP32 has its own type scale for reading at a distance.

**Floors (WCAG 2.2 AA; checked in tests, not left to judgment):**
- **Contrast:** 4.5:1 for text and 3:1 for UI parts (borders, rings, icons), in dark, light and both host themes. WCAG exempts disabled controls, but ours are dimmed so that they are understood (ruling 1), so their text stays readable.
- **Target size:** every click area is at least 24×24 CSS px, and at least 44×44 where it is a primary target on a touch layout (the ring at phone width). The hit area may be larger than what is drawn.
- **Keyboard:** a visible focus ring on every interactive element, which a theme may not remove. The focus order is the reading order, taken from the document order, with no positive `tabindex`.
- **Names:** every control has an accessible name.

---

## 13. Finish the job: do what the machine can, ask just in time for what only the person knows

**The principle:** when an action is missing something it needs, the system supplies it.
- **Machine-supplied:** if the machine can do it safely (heat the nozzle, home the axes), it does so as a visible step of the action.
- **Person-supplied:** if only the person knows it (what is on an unknown spool), the UI asks for exactly that, at the moment of the action, and then carries the action through.
- **Never** an error that hands the person a list of things to do first and expects them to press the button again.

**Why:**
- People want their job done, not a lesson in its prerequisites (1987 premise).
- Asking in the context of the task, at the point of need, beats a settings detour (principle 11; current HIG: entering data, onboarding).
- A refusal that lists steps the system could take is the system handing its own work back (principle 8 says to help solve, not describe).

**The owner, 2026-10-02:**
- "The error says 'do all these things manually'. If this is the requirement, shouldn't the system actually do them instead?"
- "Force the user to complete whatever is missing JIT if not already set."

**Here:**
- Unload on a cold printer heats to the filament's temperature as the plan's first step ("Heat the nozzle · 250 °C"), then cuts and retracts.
- A flow that needs homing homes as a step.
- Load on an unknown spool opens the spool form. Saving it stores the spool and then loads, with no second press.

**Therefore:**
- **The form at the moment of need** asks only for what is missing, prefilled with everything already known. Its button names both acts ("Save and load"). Cancel leaves everything as it was.
- **Once it is set, it is set:** the answer is stored where it belongs (Spoolman), so it is never asked again.
- **Paths with no one to ask** (a slicer toolchange, a console macro, a runout at 2 a.m.) use a documented safe default (235 °C for an unknown material). Where no default is safe, they refuse with exactly what is missing and how to supply it.
- **An error remains** only for what neither the machine nor the person can supply right now: a jam, a broken sensor, an impossible configuration (principle 8).

## Owner rulings derived from the principles (2026-09-29)

1. **Dimmed, not hidden (2, 8).** An action that does not apply now stays in its place, dimmed, with its reason. In the owner's words, "it allows a human to understand what *could* be done even if currently doesn't work". Only an action the object can never perform is omitted (no `reread` on a unit without an RFID reader). Each widget therefore has a fixed action set in a fixed order.
2. **Severity by icon as well as color (6).** Errors use an octagon with "!", information a triangle with "!", on badges and on the red and yellow message rows.
3. **One tap target per tile on the ESP32 (10).** A tap anywhere on a tile opens its action sheet, listing the ring's actions (edit) first, then the tag's (details), then the tile's (load/unload). The web keeps the direct ring/tag/tile targets as shortcuts to the same actions. Chosen for the code: the tag overlays the ring, so a small touchscreen cannot separate them, and the sheet reuses `view_tile_actions` with no hit-testing.

Also applied from principle 6: action buttons (Unload, Stop) use the neutral button style, never red text.

*Recording rule:* a new owner ruling is added to `UNIFIED_UI.md` §4 with the principle it follows. A ruling that fits none of them is a sign the principles need to change, and they are updated here.
