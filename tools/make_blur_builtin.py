#!/usr/bin/env python3
"""blur-nx (2026-11-10, from rr6-nx's make_rr6_builtin.py): the player's build's two settings files (BLUR_BUILTIN_CONFIG=ON)
from Blur's play toml, so a player's build carries Blur's settings (the pair in app/ was Katamari's: log off, MSAA 4,
its DLC key).

  app/blur_switch_defaults.toml  every setting of the play toml but the player's, compiled into the NRO
  app/blur_switch.toml           the player's blur.toml (written next to the NRO when there is none): the nine
                                 settings of PLAYER_KEYS (2026-11-18)

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
# blur-nx (2026-11-18, the user: "simplify the config to only have language (set it to system language by default)/
# framerate cap/handheld resolution/docked resolution/SMAA/MSAA/input_xbox layout/input_joycon_sideways/
# input_joycon_sideways_stick keep all the settings the same as they are for those"): those nine; and (the user, the
# same day: "make 4 core mode an option in the settings exposed to users just make it on by default") four_core_mode. disable_msaa,
# input_rumble and the button maps are built in (their values as before); the game menu (A+B+X+Y+L+R) writes a map or
# rumble into blur.toml only when the player changes it from the default.
PLAYER_KEYS = ('language', 'frame_cap', 'banjo_renut_render_height_handheld', 'banjo_renut_render_height_docked',
               'banjo_renut_smaa', 'msaa_level', 'input_xbox_layout', 'input_joycon_sideways',
               'input_joycon_sideways_stick', 'four_core_mode')
PLAYER_TABLES = ()
FINAL = {'log_level': '"off"'}

PLAYER_HEAD = '''# Blur for the Nintendo Switch (blur-nx): your settings. Everything else is built into the game.
# Change them here, or in the game menu: hold A+B+X+Y+L+R during the game, and + closes it. Language, the resolutions
# and MSAA (marked * in the menu) take effect at the next start. Delete this file and the game writes it again.

# The game's language: "System" (the console's own), "English", "French", "German", "Italian", "Spanish", "Polish",
# "Russian", "Japanese", "Korean", "Portuguese", "Chinese (Simplified)" or "Chinese (Traditional)". A language the
# game does not have shows English.
language = "System"

# Frame rate cap: "30" (a steady pace), "60" or "Off".
frame_cap = "30"

# The height of the 3D image in handheld mode: 540, 576, 612, 648, 684 or 720 (1280x720). Lower is faster; the HUD
# and text stay sharp.
banjo_renut_render_height_handheld = 720

# The height of the 3D image on the TV: 720 (1280x720), 792, 864, 900, 936, 972 (1728x972, the default), 1008 or
# 1080 (1920x1080). Lower is faster; the HUD and text are drawn at 1080p.
banjo_renut_render_height_docked = 972

# SMAA edge smoothing on the 3D image (the HUD and text stay sharp). Not the game's own picture; costs GPU time.
banjo_renut_smaa = true

# The game's own edge smoothing (MSAA, 4x on the Xbox 360): 1 = none (the default, with SMAA above: the fastest),
# 2 = 2x, 4 = the game's 4x (smoother edges, more GPU time: about 9 % lower frame rate than none).
msaa_level = 1

# Face buttons by position, as on an Xbox pad (the bottom button is A). false = by letter (the Switch's A is A).
input_xbox_layout = true

# Single Joy-Cons are held sideways, one per player.
input_joycon_sideways = true

# A sideways Joy-Con's stick turned to match the grip.
input_joycon_sideways_stick = true

# Four-core mode: the game uses all four CPU cores when started from its Home menu icon (the Sphaira forwarder).
# HIGHLY RECOMMENDED: leave it on (false is much slower). A change takes effect at the next start.
four_core_mode = true
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
