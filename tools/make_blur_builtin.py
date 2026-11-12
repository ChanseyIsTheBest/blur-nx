#!/usr/bin/env python3
"""blur-nx (2026-11-10, from rr6-nx's make_rr6_builtin.py): the player's build's two settings files (BLUR_BUILTIN_CONFIG=ON)
from Blur's play toml, so a player's build carries Blur's settings (the pair in app/ was Katamari's: log off, MSAA 4,
its DLC key).

  app/blur_switch_defaults.toml  every setting of the play toml but the player's, compiled into the NRO
  app/blur_switch.toml           the player's blur.toml (written next to the NRO when there is none): the edge smoothing
                                 choices, the pad options, [input_p1] and [input_p2]

  python tools/make_blur_builtin.py [--play dist/switch/blur-nx/renut_play.toml]

The test builds (BLUR_BUILTIN_CONFIG=OFF, docs/blur-notes.md) read a full renut.toml instead; this pair is for a
player's build. Checks that the two together give every value of the play toml (log_level off: no log but the crash
logs, rr6-nx's final build).
"""
import argparse
import os
import re
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_KEYS = ('banjo_renut_smaa', 'disable_msaa', 'input_rumble', 'input_xbox_layout', 'input_joycon_sideways')
PLAYER_TABLES = ('input_p1', 'input_p2')
FINAL = {'log_level': '"off"'}

PLAYER_HEAD = '''# Blur for the Nintendo Switch (blur-nx): your options. Every other setting is built into the game.
# Delete this file and the game writes it again at the next start.

# SMAA edge smoothing on the 3D (the HUD and text stay sharp). Not the game's own picture; costs GPU time.
banjo_renut_smaa = true

# The game's own edge smoothing (4x MSAA, as on the Xbox 360). Leave it false: true turns it off, and Blur draws
# its 3D image in strips laid out for 4x MSAA, so without it most of the picture stays black (2026-11-12).
disable_msaa = false

# Controller vibration (rumble). false = none.
input_rumble = true

# Face buttons by position, as on an Xbox pad (the bottom button is A). false = by letter (the Switch's A is A).
input_xbox_layout = true

# Single Joy-Cons are held sideways, one per player.
input_joycon_sideways = true

# Each player's buttons: the Switch buttons that press each Xbox button, for example a = "B, ZR". Empty = the default.
# Buttons: A B X Y L R ZL ZR PLUS MINUS LSTICK RSTICK UP DOWN LEFT RIGHT SL SR, or NONE.
'''


def tables(text):
    """[(name, start, end)] of the text's [table] sections, end at the next one or the end of the text."""
    heads = [(m.group(1), m.start()) for m in re.finditer(r'(?m)^\[([^\]]+)\]\s*$', text)]
    return [(n, s, heads[i + 1][1] if i + 1 < len(heads) else len(text)) for i, (n, s) in enumerate(heads)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--play', default=os.path.join(ROOT, 'dist', 'switch', 'blur-nx', 'renut_play.toml'))
    a = ap.parse_args()
    play = open(a.play, encoding='utf-8').read().replace('\r\n', '\n')
    values = tomllib.loads(play)

    sections = tables(play)
    keep = play
    for name, s, e in reversed(sections):
        if name in PLAYER_TABLES:
            keep = keep[:s] + keep[e:]
    first = re.search(r'(?m)^\[', keep)
    top, rest = (keep[:first.start()], keep[first.start():]) if first else (keep, '')
    top = re.sub(r'(?m)^(%s)\s*=.*\n' % '|'.join(PLAYER_KEYS), '', top)
    for k, v in FINAL.items():
        top, n = re.subn(r'(?m)^%s\s*=.*$' % k, f'{k} = {v}  # the player\'s build: no logs but the crash logs', top)
        assert n == 1, k
    defaults = ('# blur-nx: the player\'s build\'s fixed settings, compiled into the NRO (BLUR_BUILTIN_CONFIG=ON); the '
                'player\'s are in\n# blur.toml (app/blur_switch.toml). Made by tools/make_blur_builtin.py from ' +
                os.path.relpath(a.play, ROOT).replace(os.sep, '/') + '.\n\n' + top.rstrip('\n') + '\n' +
                ('\n' + rest.strip('\n') + '\n' if rest.strip() else ''))
    assert ')KATAMARICONFIG' not in defaults and 'katamari_' not in defaults

    player = PLAYER_HEAD
    for name, s, e in sections:
        if name in PLAYER_TABLES:
            player += '\n' + play[s:e].strip('\n') + '\n'

    d, p = tomllib.loads(defaults), tomllib.loads(player)
    for k in PLAYER_KEYS:
        assert k not in d, f'{k} left in the built-in part'
    for t in PLAYER_TABLES:
        assert t not in d and t in p, t
    merged = {**d, **p}
    for k, v in values.items():
        if k in FINAL:
            assert merged.get(k) == tomllib.loads(f'x = {FINAL[k]}')['x'], k
            continue
        assert merged.get(k) == v, f'{k}: {merged.get(k)!r} against the play toml\'s {v!r}'
    open(os.path.join(ROOT, 'app', 'blur_switch_defaults.toml'), 'w', encoding='utf-8', newline='\n').write(defaults)
    open(os.path.join(ROOT, 'app', 'blur_switch.toml'), 'w', encoding='utf-8', newline='\n').write(player)
    print(f'app/blur_switch_defaults.toml: {len(d)} keys/tables built in; app/blur_switch.toml (blur.toml): '
          f'{len(p)} ({", ".join(p)})')


if __name__ == '__main__':
    main()
