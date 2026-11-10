#!/usr/bin/env python3
"""blur-nx (2026-11-10, from rr6-nx's make_rr6_tomls.py and katamari-nx's before it): writes the package's tomls from
tools/renut_base_banjo.toml (banjo-nx's shipped renut.toml of 2026-10-29 as katamari-nx's generator left it in the base,
with Katamari's own keys taken out here), with Blur's settings:

  renut.toml       the shipped settings: Renut on deko3d, 720 lines handheld and docked (the Xbox 360's own:
                   pixel-identical; 1080 docked is a config for the user), the game's own MSAA, every exact enhancement
                   as banjo-nx ships it, the items measured SLOWER ON THE CONSOLE off, diagnostics on (logs);
  renut_play.toml  renut.toml with the diagnostics off (the log at "error", no samplers, no census lines);
  renut_ab.toml    renut.toml with an in-run A/B list, 30 s halves, target 1 (a Vulkan-only key: does nothing on the
                   Switch) as the do-nothing control;
  configs/*.toml   the verify, PGO and picture-changing configs, each one step from renut.toml.

The NRO reads blur.toml next to it (BLUR_BUILTIN_CONFIG on: these keys override the built-in ones) or renut.toml
(off). tools/package_switch.ps1 names the package's copy.

  python tools/make_blur_tomls.py --out dist/switch/blur-nx
"""
import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEADER = """# blur-nx (Blur on the Switch, Renut's renderer on deko3d): {what}
# Made by tools/make_blur_tomls.py from banjo-nx's shipped renut.toml (2026-10-29, through renut_nx_ultimate_base).
# Copy next to the NRO as blur.toml. Credits: reNut's renderer (masterspike52, reuse permitted), renut_nx_ultimate_base
# and the ports its fixes come from (katamari-nx, banjo-nx, sa2-nx, rr6-nx, sasrt-nx, kameo-nx and the others),
# NetherSX2_nx (four-core layout), battd_nx (the GPU arena), bejeweled3-nx and the zuma ports (hot functions).
"""

SHIPPED = {
    'banjo_renderer': '"renut"',
    'renut_gpu_backend': '"deko3d"',
    # The Xbox 360's 720 lines in both modes and the game's own MSAA: pixel-identical is the default (rr6-nx's rule);
    # configs/renut_1080_docked.toml is one key away, for the user to decide on.
    'banjo_renut_render_height_handheld': '720',
    'banjo_renut_render_height_docked': '720',
    'banjo_renut_dynamic_resolution': 'false',
    'disable_msaa': 'false',
    'renut_post_process_precision': '"Game"',
    # The game paces itself; no extra cap.
    'frame_cap': '"Off"',
    'target_refreshRate': '-1',
    'masseffect_renderer_native': 'false',
    'gpu_plugin': '""',
    # Three cores (no forwarder NPDM written), as rr6-nx and katamari-nx ship.
    'banjo_forwarder_program_id': '""',
    'banjo_xma_frame_cache': 'false',
    'banjo_shader_misses': '"sdmc:/switch/blur-nx/shader_misses"',
    'log_level': '"info"',
    'log_file_writer': 'false',
    # 2026-11-10: the display's gamma ramp (the device's default, from VdGetCurrentDisplayGamma), which the 360's display and
    # the reference apply. Title screen against the reference: mean difference 2.9 / 7.6 / 9.0 off, 0.3 / 1.3 / 1.5 on.
    # rr6-nx measured its present cost on the console at 0.4-0.6 ms a frame (the base's open item): the user's decision.
    'banjo_renut_game_gamma': 'true  # the ramp of the reference and the 360 display; configs/renut_gamma_off.toml: without it',
    # 2026-11-10: banjo-nx ships SMAA on (its user's call); it smooths edges, not the game's picture: off here,
    # configs/renut_smaa.toml for the user to decide on. Banjo's fur-shell cuts (also its user's calls) act only on Banjo's
    # four fur shaders (render/draw.cpp, by pixel shader hash): inert for Blur, set to the game's own all the same.
    'banjo_renut_smaa': "false  # blur-nx: off (not the game's picture; the note above is banjo-nx's); configs/renut_smaa.toml: on",
    'banjo_renut_fur_shells': '1',
    'banjo_renut_portrait_fur_shells': '1',
    'banjo_renut_fur_shells_percent': '100',
    'banjo_renut_portrait_fur_shells_percent': '100',
}
PLAY = {'log_level': '"error"', 'banjo_renut_diag_resolve_trace': 'false', 'banjo_renut_diag_gpu_shaders': 'false',
        'banjo_renut_diag_terrain': 'false', 'switch_profile_stacks': 'false', 'banjo_cpu_profiler': 'false',
        'banjo_renut_diag_fast_census': 'false', 'banjo_renut_diag_shadow_census': 'false',
        'banjo_renut_diag_unleashed_census': 'false'}
# The targets that act on Blur's draws (direct calls and recorded command buffers: docs/blur-notes.md, the census): the
# fast draw (25), pipelines by content (28), whole-buffer copies (22), the setters' touch compare (26), merged draws
# (33), mesh objects (42), resource and geometry objects (45, 48), the replay stream (46); 70 the machine copies;
# 1 (a Vulkan-only key) does nothing on the Switch: the control.
# 2026-11-10: Blur's own keys too: 211 the EXPAND/FLOAT alias, 212 the kept destination, 213 the occlusion answer, 214
# the tile resolve staging (each a correctness fix; the A/B gives its cost and its picture).
AB = {'banjo_renut_ab_targets': '"25,28,22,26,33,42,45,46,48,70,211,212,213,214,1"', 'banjo_renut_ab_s': '30',
      'banjo_renut_dynamic_resolution': 'false'}
VERIFY = {k: 'true' for k in (
    'banjo_renut_fast_draw_verify', 'banjo_renut_resource_objects_verify', 'banjo_renut_fetch_cache_verify',
    'banjo_renut_whole_buffers_verify', 'banjo_renut_persistent_shared_verify', 'banjo_renut_chain_reuse_verify',
    'banjo_renut_snapshot_deltas_verify', 'banjo_renut_store_cards_verify', 'banjo_renut_record_extents_verify')}
VERIFY.update({'banjo_hot_guard_calls': '4000', 'banjo_hot_guard_period': '64'})
PGO = {'log_level': '"off"', 'switch_profile_stacks': 'false', 'banjo_cpu_profiler': 'false',
       'banjo_show_profiler': 'false', 'banjo_show_fps': 'false', 'banjo_stall_watch_seconds': '0',
       'renut_gpu_profile': '0.0', 'banjo_renut_diag_resolve_trace': 'false', 'banjo_renut_diag_gpu_shaders': 'false',
       'banjo_renut_diag_terrain': 'false', 'banjo_hot_guard_calls': '0', 'banjo_hot_guard_period': '0'}
CONFIGS = {
    'renut_verify.toml': (VERIFY, 'every Renut verify mode on (emulator / test runs: slow; the log\'s verify lines '
                                  'must count 0 differences)'),
    'renut_pgo.toml': (PGO, 'the shipped settings with only the crash logs (the PGO build\'s toml)'),
    'renut_1080_docked.toml': ({'banjo_renut_render_height_docked': '1080'},
                               '1080 lines docked (not pixel-identical: the user\'s decision)'),
    'renut_dynamic.toml': ({'banjo_renut_dynamic_resolution': 'true'},
                           'banjo-nx\'s dynamic resolution (not pixel-identical: the user\'s decision)'),
    'renut_gamma_off.toml': ({'banjo_renut_game_gamma': 'false'},
                             "without the display's gamma ramp (brighter darks than the reference; saves the ramp's "
                             "present cost)"),
    'renut_smaa.toml': ({'banjo_renut_smaa': 'true'},
                        "SMAA 1x on the 3D image before the HUD (smooths edges: not the game's picture; costs GPU time)"),
    'renut_profile.toml': ({'switch_profile_stacks': 'true'},
                           'the console profile: the sampler on (logs/rex/rex_profile.log, one report every 10 s)'),
}


def apply(text, keys):
    """Sets each key: in place where the base has it at the top level, else before the first [section] ([input_p1] /
    [input_p2] stay last: a key after a table header belongs to that table)."""
    first_section = re.search(r'^\[', text, re.M)
    top_end = first_section.start() if first_section else len(text)
    for k, v in keys.items():
        pat = re.compile(r'^(\s*)' + re.escape(k) + r'\s*=.*$', re.M)
        m = pat.search(text, 0, top_end)
        if m:
            text = text[:m.start()] + f'{m.group(1)}{k} = {v}' + text[m.end():]
        else:
            text = text[:top_end].rstrip('\n') + f'\n{k} = {v}\n\n' + text[top_end:]
        first_section = re.search(r'^\[', text, re.M)
        top_end = first_section.start() if first_section else len(text)
    return text


def base_text():
    t = open(os.path.join(ROOT, 'tools', 'renut_base_banjo.toml'), encoding='utf-8').read()
    # katamari-nx's header and its own keys (natives, DLC list) go; so does banjo-nx's title-update note.
    t = re.sub(r'\A(#[^\n]*\n)+', '', t, count=1)
    t = re.sub(r'^katamari_[a-z_]+\s*=.*\n', '', t, flags=re.M)
    t = re.sub(r'# Title update 3 \(2026-10-29\).*?renut_guard\.toml[^\n]*\n', '', t, flags=re.S)
    t = t.replace('sdmc:/switch/katamari-nx/', 'sdmc:/switch/blur-nx/').replace('katamari-nx', 'blur-nx')
    return t


def write(path, what, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline='\n').write(HEADER.format(what=what) + text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'dist', 'switch', 'blur-nx'))
    a = ap.parse_args()
    shipped = apply(base_text(), SHIPPED)
    write(os.path.join(a.out, 'renut.toml'), 'the shipped settings, with logs (720 lines both modes, the game\'s MSAA)',
          shipped)
    write(os.path.join(a.out, 'renut_play.toml'), 'the shipped settings with the diagnostics off', apply(shipped, PLAY))
    write(os.path.join(a.out, 'renut_ab.toml'), 'the shipped settings with an in-run A/B (30 s halves, target 1 as the '
          'do-nothing control)', apply(shipped, AB))
    for name, (keys, what) in CONFIGS.items():
        write(os.path.join(a.out, 'configs', name), what, apply(shipped, keys))
    print('written to', a.out)
    # blur-nx (2026-11-11): the PC test build's renut.toml (the app reads renut.toml beside renut.exe): the shipped
    # settings, so a PC comparison with the reference is of the picture players get. Without it the PC ran the build's
    # defaults, enhancements included (renut_post_process_precision "High": the 8-bit post targets at 16 bits). The
    # one Switch path (the shader-miss folder on sdmc:) is left empty, and the back end is the PC's default, D3D12
    # (deko3d means Vulkan off the Switch; RenderDoc's captures here are D3D12). Not shipped; app/out is the build folder.
    pc = os.path.join(ROOT, 'app', 'out', 'pc')
    if os.path.isdir(pc):
        write(os.path.join(pc, 'renut.toml'), 'the PC test copy of the shipped settings (D3D12, the shader-miss folder '
              'off)', apply(shipped, {'banjo_shader_misses': '""  # PC test copy: the Switch folder is not on the PC',
                                      'renut_gpu_backend': '"d3d12"  # PC test copy: the PC default (deko3d on the '
                                                           'Switch)'}))
        print('PC test copy written to', pc)


if __name__ == '__main__':
    main()
