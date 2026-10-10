# Using the display

## What it is and why

The display shows one unit: the one it is bound to. Everything on the home
screen is that unit's, so a glance tells you what each bay holds and what the
toolhead has loaded. Other units appear only where they matter, for example a
backup spool named with its unit ("B Spool 2").

## What it adds

- Load, unload and change a bay's group with a tap.
- Edit a spool's vendor, material, color, pressure advance and weights.
- See why the printer refused an action, in the printer's own words.
- Dim or switch off the screen on a timer, with wake-on-tap.

## Requirements

A display set up per [flash-and-setup.md](flash-and-setup.md) and a printer
running the OpenAMS plugin. Spool editing needs Spoolman, which the OpenAMS
installer sets up by default.

## The screens

The source is `src/ui/screen_*.c`.

| Screen | How you reach it | What it shows |
|--------|------------------|---------------|
| Home | start screen | four tiles across (chevrons page a unit with more bays), the message row, the filament path to the toolhead |
| Menu | the button at the top left | a grid of the screens below |
| Environment | the humidity and temperature pill, or Menu, "Env" | humidity, temperature, drying state, material presets, target temperature and hours, Start and Stop |
| Alerts | the alert slot at the header's right end, or Menu | alerts for the unit, its toolhead and the panel, each with its actions; "Earlier" lists history |
| Load / unload | the message row during a load, or a tile's Load or Unload | the steps, the current one, Stop, Retry, Done |
| Print | the message row otherwise, or Menu, "Print" | the bay in use, the toolhead's message, Pause and load |
| Groups | Menu, "Groups" | this unit's groups and their order, plus bays in no group |
| Map | Menu, "Map" | each slicer tool's group and its bays; Confirm stays dim until every tool has a spool |
| Setup | Menu, "Setup" | language, the host's switches, display options |
| This unit | Menu, "This unit" | which unit this display shows; Re-scan units |
| Info | Menu, "Info" | family, config name, serial, firmware |
| Edit | a tile's Edit | spool facts for one bay |
| Debug | long-press a header title | switch the demo scenario; a technician's screen |

A tag on a tile (Not inserted, Error, Runout, Positioning) is a solid chip
over the ring. Alerts are red for a fault or pause, yellow for information.

## Tile actions

Tap a tile to open its sheet. Which rows appear depends on the bay:

| Row | Does |
|-----|------|
| Load | load that spool; unloads the current one first when needed |
| Unload | unload the toolhead |
| Edit | open the Edit screen |
| Re-read | read the spool's tag again |
| Change group... | open the group picker |
| Assign Spoolman spool | pick a Spoolman spool for an untagged bay |

A row that cannot run is dimmed. Tap it to see why: "Unit is busy, please
wait", "In use by the running print", "Spool is empty", "No tag on this
spool", "Unit is not connected".

## Refusals on the load screen

When you load or unload and the host refuses, the load screen shows the
answer: "Could not start loading:" or "Could not start unloading:", followed
by the host's own reason. The reason can be long and wraps. Nothing moved; fix
the cause and try again. A typical one asks you to heat the extruder first.

## Groups

A group is a named, ordered list of bays, usually named for a slicer tool
(`T0`). On a runout the printer falls back through the group in order. Details:
[docs/GROUPS.md](../GROUPS.md).

To change a bay's group:

1. Tap the bay's tile, then "Change group...". On the Groups screen, tap the
   bay itself.
2. Pick a group (the current one is checked), "New group" (a stepper picks the
   number), or "Remove from group".
3. A toast confirms the move.

The host can refuse a change while a lane is loading, unloading or handling a
runout, or when the group is the one loaded. The reason appears in a toast.

## Spool editing with Spoolman

The Edit screen names the bay as its tile does ("T0" or "Spool 3"). Fields are
vendor, material, color, pressure advance, and remaining and initial weight.
Save sends one action to the host (`server.openams_spoolman.edit`), which
writes to Spoolman. The web panel's form sends the same call.

Fields are dimmed, with the reason on tap, when:

- the bay's info was read from a tag ("Filament info was read from the tag");
- the bay is in the running print ("This spool is in use by the running print");
- the host cannot take the edit (Spoolman unreachable).

A refused save stays on the screen so you keep your values, with "Could not
save the spool:" and the reason. Reset clears vendor, material, color and PA;
the tile then shows `?` until you set them again.

## Touch calibration

The display calibrates itself on first boot, when no valid calibration is
stored. To redo it, open Setup, then "Calibrate touch" ("Tap four targets to
align the touchscreen"). The display clears the stored calibration and
restarts, which brings the calibration screen back. Press firmly on the center
of each target. A target too close to the last one asks you to try again.

## Display settings

In Setup, under Display:

| Setting | Options |
|---------|---------|
| Brightness | stepper, in percent |
| Contrast | Standard, High |
| Dim after | Never, 1, 5, 10 min |
| Screen off after | Never, 10, 15, 30 min |
| Rotate 180 | flips the image and the touch together |

A tap on a dimmed or dark screen only wakes it; it does not press anything.
The Setup screen also carries the language choice and the host's switches
(reading spool tags, creating Spoolman spools, applying pressure advance).
Only the languages in your build are listed.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Tiles empty after connecting | not bound to the right unit | Menu, "This unit"; or `unit <name>` on the console |
| "No response from the printer" | link dropped | check [troubleshooting.md](troubleshooting.md) |
| Load row dimmed | unit busy, offline or spool empty | tap the row for the reason |
| Edit fields dimmed | tag-read info, or a running print | wait, or edit in Spoolman |
| Touch lands in the wrong place | stale calibration | Setup, "Calibrate touch" |

## Update and uninstall

Settings live in the display's flash and survive updates
([flash-and-setup.md](flash-and-setup.md)). Reflash or erase to remove them.

## Gotchas

- The display shows one unit. Rebind it from "This unit" to look at another.
- A binding that locks (a nozzle row dimmed, "Unload first: this unit's
  filament is in the toolhead") needs the unit unloaded before it changes.
- The dim and off timers count time since the last touch.

## Where next

[The web panel](web-panel.md), [troubleshooting](troubleshooting.md).
