# H2: load/unload stage model

Status: design, revised 2026-09-28. This resolves C2's open items: C2 §5.3 (stage vocabulary, index base, when a stage and a failed stage clear), C2 item 15 (the lifetime of `runout_from`), and C3's `OAMSM_STAGE` lane inference. It is host-side only.

F-3 corrected two premises of the first draft:
- no macro heats the nozzle (`_TX` and `SAFE_UNLOAD_FILAMENT` refuse when cold, `oams_macros.cfg:70-71`, `:161-163`), so there is no `heat` step;
- the unload macros receive no `GROUP=` (they use the single lane `fps`).

## 1. Two kinds of "stage"
- **The operation plan (user-facing):** the steps a toolchange or unload goes through, reported by the macros and by the manager's own ops. Every UI's stepper renders the **published plan**. UIs must not hard-code step lists; the display's dummy scenarios may keep theirs for the demo only.
- **The firmware motion stage (diagnostic):** the unit's 15-value `motion_stage` (C1 §3), published separately (§6). It is never a plan step.

## 2. Vocabulary (fixed, part of C2 v2; UIs map names to their own labels)
| Name | Meaning, mapped to `oams_macros.cfg` |
|---|---|
| `cut` | the tip retract (`G1 E-retract_length`), the cutter move (`CUT_FILAMENT`), and the post-cut extruder retract |
| `retract` | the firmware unload op pulling the filament back through the path, plus the unload sensor checks (`_OAMS_FINISH_UNLOAD`, `_OAMS_CONFIRM_UNLOADED`) |
| `feed` | the firmware load op, the inlet check, `CLEAN_NOZZLE`, the extruder reload (`RELOAD_LENGTH`), and the outlet check |
| `purge` | a real purge. **No macro purges today**, so no plan contains it until one does. |
| `calibrate` | a PTFE-length calibration |

**Plans** (each an ordered subset):
| Flow | Plan |
|---|---|
| switching groups (`_TX`, `SWITCH` true, something loaded) | `cut, retract, feed` |
| load into an empty toolhead (`_TX`, `LOADED_GROUP` none) | `feed` |
| unload (`SAFE_UNLOAD_FILAMENT`, `OPENAMS_UNLOAD`) | `cut, retract` |
| bare `OAMSM_LOAD_FILAMENT` / `OAMSM_UNLOAD_FILAMENT` (no macro) | auto `feed` / auto `retract` (rule 2) |
| runout reload | auto `feed`, created on entering `RUNOUT_LOADING` (not `pausing`/`coasting`, which are not steps) |
| calibration | auto `calibrate` |

`_TX` with `SWITCH` false (same group) or `EXCLUDED` emits **no** `OAMSM_STAGE` at all.

## 3. The published fields (C2 v2, per lane: `lanes_by_fps[fps].stage`, and `openams_ui` `lane.stage`)
`stage: null | {"plan": [names], "index": int | null, "failed": name | null}`
- `index` is **0-based** into `plan`, **monotonic** within a plan. `null` means announced but not started.
- `failed` names the step that did not complete. `message` carries the cause.
- `null` when no staged operation exists.

## 4. The command: `OAMSM_STAGE` (never raises)
- `OAMSM_STAGE PLAN=cut,retract,feed [FPS=<lane> | GROUP=<g>]` begins a plan on the lane. It replaces any previous one; `index=null`, `failed=null`, `stage_auto=false`.
- `OAMSM_STAGE NAME=<step> [...]` enters that step (`index` = its position).
- `OAMSM_STAGE FAILED=1 [...]` marks the current step failed.
- `OAMSM_STAGE DONE=1 [...]` ends the plan.

**Lane resolution order:**
1. `FPS=`;
2. else `GROUP=`, through the manager's group→lane map;
3. else the sole lane;
4. else the single lane with an active plan or an op in flight (`_resolve_fps` semantics);
5. else respond with a warning and do nothing.

**`OAMSM_STAGE` never raises.** Each of these is a warning and a no-op, so a stage mistake can never abort a toolchange:
- an unresolvable lane;
- an unknown step name;
- a `NAME=` earlier than the current `index`;
- a `FAILED` / `DONE` with no plan;
- `FAILED=1` while `index = null`.

The pinned-Klipper macro suite asserts on the published `stage`; that is how such mistakes are caught.

## 5. Rules
1. **Information only.** `motion_settled` / `supported_actions` are derived from `op`, `runout`, `following`, faults and connectivity (C2 §4.2, §5.6), never from `stage`. In the reducer, no branch reads `stage_*` except the `Stage*` actions, `_complete`, `ClearErrors` and the `Tick` staleness drop. The `Stage*` actions return no effects and are not in `WORLD_ACTIONS`. A UI may *withhold* an action because of `stage`; it never *offers* one because of it.
2. **The manager's own ops.** When the manager starts a firmware load, unload or calibration on a lane:
   - with an active plan (`stage_auto=false`): it enters `feed` / `retract` / `calibrate`, if present;
   - without one: it creates the one-step plan (`stage_auto=true`). On success it ends that plan; on failure rule 4 keeps it.
   - A **synchronous refusal** (no generation started: lane busy, hub occupied, a `_load_guard` refusal) never creates a plan. With an active plan, it marks the current step failed.
3. **Failure attribution.**
   - A firmware op that fails (refused, errors, times out, canceled) sets `failed` to the current step, and `message` to the cause.
   - A macro `FAILED=1` does the same. **The first failure wins**; later `FAILED` calls are ignored until a new `PLAN`.
   - A completion, timeout or cancel **never ends** a plan with `stage_auto=false`: the macro continues or fails it.
   - `failed` means "the step did not complete", not "the hardware malfunctioned".
4. **Clearing:**
   - `DONE`, a new `PLAN`, `OAMSM_CLEAR_ERRORS`, and a Klipper restart or shutdown clear `stage` (it is not persisted);
   - a plan with `failed = null` and no update for 10 minutes while `motion_settled` is dropped (a host constant; a guard against aborted macros);
   - **a failed plan is never dropped by time.**
5. **`runout_from` / `reload_target`.**
   - `runout_from` is set to `unit` on entering `pausing` (overwriting a previous value on a second runout);
   - `reload_target` is set on entering `loading`;
   - both are kept through success, failure, and the `UNLOADED` that follows a failed reload;
   - they are cleared only by the next user-initiated load or unload on that lane, or by `OAMSM_CLEAR_ERRORS`, never by time.

   This does not interfere with the active-spool switching (C4 §7.2 keys on `unit` changing while `LOADED`), and it gives F-4 the input it needs if usage is ever split at a runout.

## 6. Diagnostics
`lanes_by_fps[fps].motion_stage`: the loaded or busy unit's firmware `motion_stage` name (C1 §3), or `null`. It is diagnostic only.

## 7. Implementation notes (Fork 3, H stream)
- `LaneState` gains `stage_plan: Tuple[str, ...]`, `stage_index: Optional[int]`, `stage_failed: Optional[str]`, `stage_auto: bool` and `stage_since: float`.
- New pure actions: `StagePlan(fps, plan)`, `StageEnter(fps, name)`, `StageFail(fps)`, `StageDone(fps)`. They return no effects.
- `_complete` / `Timeout` / `Cancel` apply rules 2-3. `ClearErrors` applies rule 4. `Tick` applies the rule-4 staleness drop. The runout branch applies rule 5 and the auto `feed`.
- **Macros:**
  - `_TX` announces its plan after deciding `SWITCH` (only when switching) and enters each step at its boundary;
  - the unload macros pass `FPS=fps` (matching their existing single-lane assumption);
  - in the concurrent path, `OAMSM_STAGE NAME=feed` goes **before** `START`, not inside the routine.
- **Tests:** reducer unit tests for every rule, and the pinned-Klipper macro suite asserting the published `stage` through a switch, an unload, a load from empty, and a failed feed.
- **Display (D5):** render the published plan and map the names to labels (cut → "Cut filament", retract → "Pull back current filament", feed → "Push new filament into extruder", purge → "Purge old filament", calibrate → "Calibrate PTFE length").
