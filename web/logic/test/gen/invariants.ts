// The rules a view must obey, checked on what the core returns. Every check
// reads fields index.d.ts declares; an empty result means the view is sound.
import type { View, ViewAction, ViewTile, ViewUnit } from '../../../core/index.js';

/** @p v with its unassigned units, paired with where they are. */
function allUnits(v: View): { unit: ViewUnit; where: string }[] {
  const out: { unit: ViewUnit; where: string }[] = [];
  for (const th of v.toolheads) for (const u of th.units) out.push({ unit: u, where: `toolhead ${th.id}` });
  for (const u of v.unassigned_units) out.push({ unit: u, where: 'unassigned' });
  return out;
}

function tiles(v: View): { tile: ViewTile; unit: ViewUnit; where: string }[] {
  const out: { tile: ViewTile; unit: ViewUnit; where: string }[] = [];
  for (const { unit, where } of allUnits(v)) {
    for (const b of unit.bays) out.push({ tile: b, unit, where });
  }
  return out;
}

function allActions(v: View): { action: ViewAction; where: string }[] {
  const out: { action: ViewAction; where: string }[] = [];
  const push = (list: ViewAction[] | undefined, where: string) => {
    for (const a of list ?? []) out.push({ action: a, where: `${where} ${a.id}` });
  };
  for (const th of v.toolheads) {
    push(th.actions, `toolhead ${th.id}`);
    push(th.alert?.items, `toolhead ${th.id} alert`);
    for (const u of th.units) {
      push(u.actions, `unit ${u.id}`);
      push(u.alert?.items, `unit ${u.id} alert`);
      for (const b of u.bays) push(b.actions, `tile ${b.slot_id}`);
    }
  }
  for (const u of v.unassigned_units) {
    push(u.actions, `unit ${u.id}`);
    push(u.alert?.items, `unit ${u.id} alert`);
    for (const b of u.bays) push(b.actions, `tile ${b.slot_id}`);
  }
  for (const a of v.alerts) push(a.actions, `panel alert ${a.code}`);
  for (const s of v.settings) {
    push([s.action_on, s.action_off].filter((a): a is ViewAction => a !== null), `setting ${s.key}`);
  }
  return out;
}

function alertLevels(v: View): Map<string, Set<string>> {
  const levels = new Map<string, Set<string>>();
  const add = (text: string, level: string) => {
    if (!levels.has(text)) levels.set(text, new Set());
    levels.get(text)!.add(level);
  };
  for (const a of v.alerts) add(a.text, 'panel');
  for (const th of v.toolheads) {
    for (const a of th.alert?.items ?? []) add(a.text, 'toolhead');
    for (const u of th.units) for (const a of u.alert?.items ?? []) add(a.text, 'unit');
  }
  for (const u of v.unassigned_units) for (const a of u.alert?.items ?? []) add(a.text, 'unit');
  return levels;
}

/**
 * The rules: one loaded spool per toolhead, spare numbers consecutive within a
 * group, a reason on every disabled action, one level per alert text, and
 * hatching only where it belongs.
 */
export function checkView(v: View): string[] {
  const bad: string[] = [];

  for (const th of v.toolheads) {
    const loaded = th.units.flatMap((u) => u.bays.filter((b) => b.state === 'loaded'));
    if (loaded.length > 1) {
      bad.push(`${th.id}: ${loaded.length} loaded tiles (${loaded.map((b) => `${b.slot_id} ${b.tool}`).join(', ')})`);
    }
  }

  // Spare numbers: the member the lane holds is the bare infinity (a runout
  // keeps it, and a bay the lane is loading or unloading keeps it too), the
  // rest are 1, 2, ... with no gaps.
  const groups = new Map<string, ViewTile[]>();
  for (const th of v.toolheads) {
    for (const u of th.units) {
      for (const b of u.bays) {
        if (!b.tool || !b.spare) continue;
        const key = `${th.id}/${b.tool}`;
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key)!.push(b);
      }
    }
  }
  for (const [key, members] of groups) {
    const bare = members.filter((b) => b.spare!.position === null);
    if (bare.length > 1) bad.push(`${key}: ${bare.length} bare infinity badges (${bare.map((b) => b.slot_id).join(', ')})`);
    for (const b of bare) {
      if (b.state !== 'loaded' && b.state !== 'runout' && b.state !== 'loading' && b.state !== 'unloading') {
        bad.push(`${key}: ${b.slot_id} shows the bare infinity but is ${b.state}`);
      }
    }
    const counts = new Set(members.map((b) => b.spare!.count));
    if (counts.size > 1) bad.push(`${key}: spare counts ${[...counts].join(', ')} disagree`);
    const count = members[0].spare!.count;
    const numbered = members.map((b) => b.spare!.position).filter((p): p is number => p !== null).sort((a, b) => a - b);
    const wanted = Array.from({ length: count }, (_v, i) => i + 1);
    if (numbered.length !== wanted.length || numbered.some((p, i) => p !== wanted[i])) {
      bad.push(`${key}: spare numbers ${numbered.join(', ') || '-'} are not 1..${count} with no gaps`);
    }
  }

  for (const { action, where } of allActions(v)) {
    if (action.enabled === false && !action.reason) bad.push(`${where}: disabled with no reason`);
  }

  for (const [text, levels] of alertLevels(v)) {
    if (levels.size > 1) bad.push(`alert "${text}" is shown at ${[...levels].join(' and ')}`);
  }

  for (const { tile, unit, where } of tiles(v)) {
    if (tile.hatched && unit.online && tile.tag?.text !== 'Empty') {
      bad.push(`${where}/${tile.slot_id}: hatched with tag ${tile.tag ? `"${tile.tag.text}"` : 'none'}`);
    }
  }

  return bad;
}
