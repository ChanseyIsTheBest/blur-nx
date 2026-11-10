#!/usr/bin/env python3
"""blur-nx (2026-11-10): Renut's hook map, Katamari's addresses (the base, XDK 2.0.5632) -> Blur's (XDK 2.0.11164).

Sources, in order of trust:
  1. the state tables (tools/renut_state_tables.py with --ref-render 82AD8E48 --ref-sampler 82AD9308): every getter
     and setter paired by its record's index and default (out/renut_map/state_tables_kat2blur.txt);
  2. the Direct3D name in the hook's comment, through guwange-nx's named list for the same XDK build (guwange-nx
     app/src/renut_engine/guwange_d3d_functions.txt), each paired with Blur's code by exact shape
     (tools/blur_d3d_match.py: the same instructions with branch targets and lis-built globals left out;
     out/blur_d3d_vs_guwange.txt);
  3. worldkeflings-nx's names for the same XDK (out/world_d3d_by_name.json there), paired with Blur the same way
     (out/blur_d3d_vs_world.txt);
  4. Katamari's function against Blur's by loose shape (opcodes and registers, difflib ratio:
     out/renut_map/kat2blur_other.txt), accepted at >= 0.90 with a margin of 0.10 over the next;
  5. by hand: tools/blur_hook_map_hand.txt ("<katamari address> <blur address or -> <evidence>").
A hook with none of these is left unhooked ('-') and listed for a look by hand.

  python tools/blur_hook_map.py  -> out/renut_map/hook_map_blur.txt (katamari blur source evidence)
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, 'out', 'renut_map')


def parse_match(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        p = line.split()
        if len(p) < 3:
            continue
        name, ref, hit = p[0], p[1], p[2]
        rest = ' '.join(p[3:])
        out[(name, ref)] = (hit, rest)
    return out


def main():
    rows = json.load(open(os.path.join(ROOT, 'out', 'base_hook_sites.json')))
    state = {}
    for line in open(os.path.join(MAP, 'state_tables_kat2blur.txt')):
        if line.startswith('#'):
            continue
        p = line.split()
        state[p[3]] = (p[4], f'state table: {p[0]} {p[1]} {p[2]} (Blur record {p[5]})')
    guw = {}
    for (name, ref), (hit, rest) in parse_match(os.path.join(ROOT, 'out', 'blur_d3d_vs_guwange.txt')).items():
        if rest.startswith('exact ('):
            guw[name] = (hit, f'guwange-nx {name} {ref}, {rest}')
    world = {}
    for (name, ref), (hit, rest) in parse_match(os.path.join(ROOT, 'out', 'blur_d3d_vs_world.txt')).items():
        if rest.startswith('exact ('):
            world[name] = (hit, f'worldkeflings-nx {name} {ref}, {rest}')
    kat = {}
    for (name, ref), (hit, rest) in parse_match(os.path.join(MAP, 'kat2blur_other.txt')).items():
        m = re.match(r'(exact|loose) ([0-9.]+)?', rest)
        if rest.startswith('exact ('):
            kat[ref] = (hit, f'Katamari {ref} exact shape')
        elif rest.startswith('loose'):
            sc = re.match(r'loose ([0-9.]+) \(next ([0-9.]+)', rest)
            if sc and float(sc.group(1)) >= 0.90 and float(sc.group(1)) - float(sc.group(2)) >= 0.10:
                kat[ref] = (hit, f'Katamari {ref} {rest}')
            else:
                kat.setdefault('weak:' + ref, (hit, f'Katamari {ref} {rest}'))
    hand = {}
    hp = os.path.join(ROOT, 'tools', 'blur_hook_map_hand.txt')
    if os.path.exists(hp):
        for line in open(hp, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            p = line.split(None, 2)
            hand[p[0]] = (p[1], 'hand: ' + (p[2].strip() if len(p) > 2 else ''))
    out = []
    unresolved = []
    conflicts = []
    for r in rows:
        addr = r[3]
        name = (r[5] or r[4] or '?').replace('_hook', '')
        cands = []
        if addr in hand:
            cands.append(hand[addr])
        if addr in state:
            cands.append(state[addr])
        if name in guw:
            cands.append(guw[name])
        if name in world:
            cands.append(world[name])
        if addr in kat:
            cands.append(kat[addr])
        if not cands:
            weak = kat.get('weak:' + addr)
            unresolved.append(f'{addr} {name}  {weak[1] if weak else "no candidate"}')
            out.append(f'{addr} - none {name}')
            continue
        chosen = cands[0]
        others = {c[0] for c in cands[1:]}
        if others and others != {chosen[0]}:
            conflicts.append(f'{addr} {name}: ' + ' | '.join(f'{c[0]} ({c[1]})' for c in cands))
        out.append(f'{addr} {chosen[0]} {name}  {chosen[1]}' + (f'; agrees: {len(cands) - 1}' if len(cands) > 1 else ''))
    open(os.path.join(MAP, 'hook_map_blur.txt'), 'w').write('\n'.join(out) + '\n')
    print(len(out), 'sites;', sum(1 for l in out if ' - none ' not in l), 'mapped;', len(unresolved), 'unresolved;',
          len(conflicts), 'conflicts')
    for l in conflicts:
        print('CONFLICT', l)
    for l in unresolved:
        print('UNRESOLVED', l)


if __name__ == '__main__':
    main()
