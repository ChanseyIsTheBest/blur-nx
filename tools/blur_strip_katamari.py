#!/usr/bin/env python3
"""blur-nx (2026-11-10): takes what belonged to Beautiful Katamari (and the N&B / NFS:MW leftovers the base carries) out
of the copy of renut_nx_ultimate_base. One-off. rr6-nx's tools/rr6_strip_katamari.py did the same to the same lineage
(its list is pacman-nx's); this follows it:

 1. Sources no CMake file names (other games' addresses): "Stale mentions" in docs/optimising-functions.md, and
    katamari_stage_diag.cpp (Katamari's stage diagnostics, built: its CMake line goes too).
 2. The hot lists and natives: emptied, the common code kept. banjo_hand_natives.h and banjo_hot_guest.cpp come from
    rr6-nx's ultimate branch (its strip of the same files) with rr6-nx's own three natives taken out as well, so only
    banjo-nx's memcpy native's code is left and hand.txt lists nothing (no console profile of Blur yet).
 3. pgo/, the icon, the forwarder NPDM: removed. Function lists and analysis lists: emptied.
 4. katamari_* file names and identifiers in CMake and the app: blur_*.

  python -I tools/blur_strip_katamari.py
"""
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RR6 = r'D:\ridge racer 6\rr6-nx'

DELETE = """app/src/cpu/banjo_pool_lock.cpp
app/src/cpu/banjo_reverb_native.cpp
app/src/cpu/banjo_xaudio_native.cpp
app/src/cpu/katamari_stage_diag.cpp
app/src/me/banjo_me_hooks.cpp
app/src/native/banjo_d3d_hooks.cpp
app/src/native/banjo_espera_anillo.cpp
app/src/native/banjo_nativo_stubs.cpp
app/src/native/banjo_shader_harvest.cpp
app/src/native/nfsmw_nativo_destinos.cpp
app/src/native/nfsmw_nativo_dibujos.cpp
app/src/native/nfsmw_nativo_ganchos.cpp
app/src/native/nfsmw_nativo_shaders.cpp
app/src/native/nfsmw_nativo_sistema.cpp
app/src/native/nfsmw_nativo_texturas_pool.cpp
app/src/native/nfsmw_shader_library.cpp
app/src/renut_engine/StopNSwap.cpp
app/src/renut_engine/cvar_menu.cpp
app/src/renut_engine/env_map_pool.cpp
app/src/renut_engine/frameHooks.cpp
app/src/renut_engine/frontend_debug_menu.cpp
app/src/renut_engine/hooks.cpp
app/src/renut_engine/shader_dump.cpp
app/src/renut_engine/texture_tools.cpp
app/src/renut_engine/thread_slots.cpp
app/src/renut_engine/title_update.cpp
app/src/renut_engine/townsfolk.cpp
tools/katamari_files.py
tools/katamari_shader_files.py
tools/cpdb.py
tools/forwarder/katamari_forwarder.json
tools/forwarder/katamari_main.npdm
tools/renut_hook_map_katamari.txt
docs/console-rc3-breakdown.md
docs/console-rc4-breakdown.md
docs/console-rc5-breakdown.md
docs/console-rc9-breakdown.md""".split()

COPY = [
    'app/src/cpu/hot/banjo_hand_natives.h',
    'app/src/cpu/banjo_hot_guest.cpp',
    'app/src/cpu/banjo_hot_guest.h',
    'app/config/hot_edges.txt',
    'app/config/hot_functions.txt',
    'app/config/locals_optout.txt',
    'app/config/lr_write_same_choice.json',
    'app/config/r1_local_choice.json',
    'app/config/renut_crt.toml',
    'app/config/renut_hooks.toml',
    'app/indirect_promotions.txt',
]

RENAME = {
    'app/katamari_switch.toml': 'app/blur_switch.toml',
    'app/katamari_switch_defaults.toml': 'app/blur_switch_defaults.toml',
    'app/src/katamari_keys.cpp': 'app/src/blur_keys.cpp',
    'app/src/katamari_builtin_config.h.in': 'app/src/blur_builtin_config.h.in',
    'tools/make_katamari_tomls.py': 'tools/make_blur_tomls.py',
    'dist/switch/katamari-nx': 'dist/switch/blur-nx',
    'docs/katamari-notes.md': 'docs/katamari-notes-reference.md',
}

SUBS = [
    (r'KATAMARI_BUILTIN_TEXT', 'BLUR_BUILTIN_TEXT'),
    (r'KATAMARI_PLAYER_TEXT', 'BLUR_PLAYER_TEXT'),
    (r'KATAMARI_BUILTIN_CONFIG', 'BLUR_BUILTIN_CONFIG'),
    (r'kKatamariBuiltInConfig', 'kBlurBuiltInConfig'),
    (r'kKatamariPlayerConfig', 'kBlurPlayerConfig'),
    (r'katamari_builtin_config', 'blur_builtin_config'),
    (r'katamari_switch_defaults', 'blur_switch_defaults'),
    (r'katamari_switch\.toml', 'blur_switch.toml'),
    (r'src/katamari_keys\.cpp', 'src/blur_keys.cpp'),
    (r'\s*src/cpu/katamari_stage_diag\.cpp\n', '\n'),
    (r'/switch/katamari-nx/', '/switch/blur-nx/'),
    (r'katamari-nx\.nro', 'blur-nx.nro'),
    (r'katamari-nx\.elf', 'blur-nx.elf'),
    (r'"Beautiful Katamari"', '"Blur"'),
    (r'katamari\.toml', 'blur.toml'),
    (r'tools/switch/icon/katamari_icon\.jpg', 'tools/switch/icon/blur_icon.jpg'),
    (r'/switch/katamari_nx/', '/switch/blur_nx/'),
]
SUB_FILES = ['app/CMakeLists.txt', 'app/src/renut_app.h', 'app/src/blur_keys.cpp', 'app/src/blur_builtin_config.h.in',
             'app/src/renut_engine/dlc_install.cpp', 'app/src/renut_engine/gpu/banjo_renut_deko3d.cpp',
             'app/cmake/banjo_renut_gpu.cmake', 'app/blur_switch.toml', 'app/blur_switch_defaults.toml',
             'tools/run_ryujinx.ps1', 'tools/run_ryujinx_rdc.ps1', 'tools/run_pc.ps1', 'tools/run_pc_verify.ps1',
             'tools/run_pc_renut.ps1', 'tools/package_switch.ps1', 'tools/gate_ryujinx.ps1', 'tools/build_switch_release.bat',
             'tools/make_blur_tomls.py', 'tools/make_test_toml.py', 'tools/build_pc.bat', 'tools/build_switch.bat',
             'app/src/main.cpp']


def rd(p):
    return open(p, encoding='utf-8', errors='surrogateescape', newline='').read()


def wr(p, t):
    open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)


def strip_rr6_natives():
    # The header: rr6-nx's natives start at its "Ridge Racer 6's hand-written natives" banner and run to the end of
    # namespace banjo::hand.
    p = os.path.join(ROOT, 'app/src/cpu/hot/banjo_hand_natives.h')
    t = rd(p)
    nl = '\r\n' if '\r\n' in t else '\n'
    lines = t.split(nl)
    start = next(i for i, l in enumerate(lines) if "Ridge Racer 6's hand-written natives" in l) - 1
    end = max(i for i, l in enumerate(lines) if l.startswith('}  // namespace banjo::hand'))
    lines = lines[:start] + [
        '// ---------------------------------------------------------------------------------------------------------------------',
        '// blur-nx (2026-11-10): no hand-written natives of Blur\'s own: those come only from a console profile, past the gate',
        '// of docs/optimising-functions.md (hand.txt has the evidence of each). banjo-nx\'s memcpy native above stays, unwired.',
        '// ---------------------------------------------------------------------------------------------------------------------',
        ''] + lines[end:]
    t = nl.join(lines)
    t = re.sub(r'enum : uint32_t \{\s*kMemcpy = 0,\s*kMatMul = 1,\s*kMatTranspose = 2,\s*kVecMat = 3,\s*kNatives = 4\s*\};',
               'enum : uint32_t {' + nl + '  kMemcpy = 0,' + nl + '  kNatives = 1' + nl + '};', t)
    t = t.replace('// rr6-nx (2026-11-08): the first four, from the console profile of 2026-11-05 (hand.txt): banjo-nx\'s memcpy native for' + nl +
                  '// this game\'s C runtime memcpy, and three routines of the game\'s matrix library (the end of this file).',
                  '// blur-nx (2026-11-10): banjo-nx\'s memcpy native only (rr6-nx\'s list, its own three natives taken out).')
    wr(p, t)
    p = os.path.join(ROOT, 'app/src/cpu/banjo_hot_guest.cpp')
    t = rd(p)
    t = re.sub(r"// rr6-nx \(2026-11-08\): Ridge Racer 6's own natives.*?REXCVAR_DEFINE_BOOL\(rr6_hand_vector_transform.*?\.lifecycle\(rex::cvar::Lifecycle::kInitOnly\);",
               "// blur-nx (2026-11-10): no natives of Blur's own yet (only from a console profile).", t, flags=re.S)
    t = re.sub(r'constexpr const char\* kNames\[kNatives\] = \{[^}]*\};', 'constexpr const char* kNames[kNatives] = {"memcpy"};', t)
    t = re.sub(r'constexpr const char\* kKeys\[kNatives\] = \{[^}]*\};', 'constexpr const char* kKeys[kNatives] = {"banjo_hand_memcpy"};', t)
    t = re.sub(r'return which == kMemcpy\s*\? bool\(REXCVAR_GET\(banjo_hand_memcpy\)\).*?: false;',
               'return which == kMemcpy ? bool(REXCVAR_GET(banjo_hand_memcpy)) : false;', t, flags=re.S)
    t = t.replace("// rr6-nx (2026-11-08): the first four (hot/hand.txt, from the console profile of 2026-11-05). Each one adds its enum",
                  "// blur-nx (2026-11-10): banjo-nx's memcpy only (rr6-nx's list less its own three). Each native adds its enum")
    wr(p, t)
    p = os.path.join(ROOT, 'app/src/cpu/hot/hand.txt')
    wr(p, '# blur-nx: guest functions with a HAND-WRITTEN native (app/src/cpu/hot/banjo_hand_natives.h, namespace\n'
          '# banjo::hand::n_<id>). Only from a console profile, past the gate of docs/optimising-functions.md, with the evidence\n'
          '# in the line\'s comment: profile (build), scene, thread, share, what the samples showed.\n'
          '#\n'
          '#   <address or generated name> [id=<namespace suffix>] [exact=<registers>]   # evidence\n'
          '#\n'
          '# None yet: no console profile of Blur (2026-11-10).\n')
    for name in ('localized.txt', 'localized_bulk.txt'):
        wr(os.path.join(ROOT, 'app/src/cpu/hot', name),
           '# blur-nx: machine copies (tools/localize_native.py; the bulk list by tools/switch_hot_bulk.py --merge,\n'
           '# docs/optimising-functions.md).\n')


def main():
    for rel in DELETE:
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            os.remove(p)
    for rel in COPY:
        shutil.copyfile(os.path.join(RR6, rel), os.path.join(ROOT, rel))
        p = os.path.join(ROOT, rel)
        t = rd(p)
        wr(p, t)
    strip_rr6_natives()
    pgo = os.path.join(ROOT, 'pgo')
    if os.path.isdir(pgo):
        shutil.rmtree(pgo)
    os.makedirs(pgo, exist_ok=True)
    wr(os.path.join(pgo, 'README.md'), '# pgo/\n\nThe PGO profile of the released NRO goes here (docs/pgo-lto.md). None yet for Blur.\n')
    icon = os.path.join(ROOT, 'tools/switch/icon/katamari_icon.jpg')
    if os.path.exists(icon):
        os.remove(icon)
    for a, b in RENAME.items():
        pa, pb = os.path.join(ROOT, a), os.path.join(ROOT, b)
        if os.path.exists(pa):
            shutil.move(pa, pb)
    for rel in SUB_FILES:
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        t = rd(p)
        n = t
        for a, b in SUBS:
            n = re.sub(a, b, n)
        if n != t:
            wr(p, n)
    print('done')


if __name__ == '__main__':
    main()
