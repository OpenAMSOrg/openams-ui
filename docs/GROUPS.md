# Filament groups and infinite spooling

How the display models filament groups, following OpenAMS (`~/klipper_openams`:
`src/filament_group.py`, `src/oams_topology.py`, `src/oams_state.py`,
`src/oams_manager.py`, and the v1 UI API in its history, `docs/UI_API.md`).

## What a group is

- A **group** is a named tool or material request (`T0`, `T1`, or any name of
  letters, digits, `_`, `-`, `.`). Its value is an **ordered** list of bays,
  e.g. `[filament_group T0] group: oams1-0, oams2-2`.
- Members can sit on several units: 4-bay OAMS units and single-bay followers.
  All members of one group must be on the **same FPS lane** (one lane per
  toolhead; a multi-toolhead printer has one lane per toolhead).
- A bay belongs to **at most one** group. A group may be empty (declared, not
  yet populated).
- The OpenAMS v1 status publishes `groups: [{name, lane, slots}]`, each lane's
  `current_group` and `current_slot`, and a top-level `current_group`.

## Loading a group (printing a tool)

- The slicer's tool `Tn` loads group `Tn`.
- The bay loaded is the member already loaded, if there is one; otherwise the
  **first `ready` member in declared order**.
- Selecting a different member of the same group is a toolchange. A load is
  never sent for a group with no ready member.

## Infinite spooling (runout auto-reload)

- When the loaded bay runs out while printing, OpenAMS pauses feeding, coasts
  the remaining filament through the PTFE, then auto-loads the **next `ready`
  member of the same group in declared order**, never the bay that just ran
  out.
- There is **no on/off switch**: the group's order is the whole policy. A group
  with one member has no spare.
- No ready spare: the print pauses ("filament runout on group T0 and no spare
  spool available").
- `ptfe_length` not calibrated: the print pauses instead of auto-loading.

## Editing groups

- Create and delete groups; assign and unassign bays.
- Edits persist to the OpenAMS config immediately (no SAVE_CONFIG) and take
  effect live.
- Moving a bay into a group removes it from its previous group (the donor).
- Edits are **refused** while:
  - the lane is loading, unloading or calibrating;
  - the lane is handling a runout;
  - the destination or donor group is the one loaded on its lane, because
    changing its membership would change which spool loads next.

## How the display maps it

The display is mounted on ONE unit. A group matters to it only through this
unit's bays.

- **Model:**
  - named ordered groups replace the per-slot `refill_group` number;
  - `g_mmu.current_group` is the group loaded in the toolhead;
  - helpers: the group of a bay; the group's next spare (what auto-reload
    would load now); whether a group is editable (the rules above).
- **Home tiles:** a small group tag ("T0") on every bay that belongs to one.
- **Slot sheet:**
  - shows "T0 · next spare B2 (unit B)" (or "no spare");
  - "Change group…" only when editing is allowed (per the owner's
    show-only-what-is-available rule).
- **Groups screen** (the former auto-refill screen):
  - every group that includes one of this unit's bays, members in order, with
    loaded / ready / empty state and "next on runout";
  - members on other units are labelled with their unit;
  - this unit's unassigned bays get "Add to group" (the sheet lists groups on
    this unit's lane, plus "New group");
  - no auto-refill toggle.
- **Print mapping:**
  - the print's tools T0..Tn, each with its group's members;
  - this unit's bays are highlighted;
  - a tool whose group has no ready member is flagged.
- **Toolhead row / runout:** "T0 · A1 ran out → B2" names the group.
