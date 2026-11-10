#!/usr/bin/env python3
"""blur-nx (2026-11-10): the guest data addresses and game functions Renut hard-codes, from Beautiful Katamari's (the
base) to Blur's. Each was paired on Blur's own image (docs/blur-notes.md, "Renut at XDK 2.0.11164"):

  Katamari      Blur        what                         evidence
  0x82AD8E48 -> 0x82B9A888  render state table x101      tools/renut_state_tables.py: all 101 records paired by default
  0x82AD9308 -> 0x82B9AD48  sampler state table x20      the same: all 20
  0x820FA9D0 -> 0x82020D38  format table                 tools/blur_data_refs.py: 6 paired functions agree (Lock2D/3DSurface,
                                                           GetLevelLayout, GetSubresourceLayout, FindTextureSize, ...)
  0x820FAEB4 -> 0x82020CC0  full viewport                tools/blur_data_refs.py (SetSurfaceInfo's pair); the same 24 bytes
  0x82AD93F8 -> 0x82B9AE90  XDK build qword              holds 0x000200002B9C4000 (2.0.11164.0), the image's only one
  0x82000750 -> 0x820008A4  VdGlobalDevice's record      LockResource 82246230 reads 0x820008A4 for a title (KeGetCurrentProcessType
                                                           != 2; 0x820008A0, VdGlobalXamDevice, for the system); its loaded value
                                                           0xFFCAE000 is Katamari's record's
  0x8315DFE0 -> 0x82C82B4C  XPhysicalAlloc no-large-pages XPhysicalAlloc 82237170 (Katamari 824C1C30): the same instructions,
                                                           `lis r11,0x82C8; lwz r11,11084(r11)` where Katamari reads -8224(0x8316)
  0x8315E830 -> 0x82C82B58  HiZ owner                    tools/blur_data_refs.py: CreateSurface's pair, twice
  sub_824C1C10 -> sub_829BA5D8  GetCurrentThreadId       the same three instructions (`lwz r11,256(r13); lwz r3,332(r11); blr`),
                                                           the image's only copy
  sub_824CAB98 -> sub_8217CAA0  XMemFree                 the device init frees with `lis r4,0xB180; li r3,0; bl 0x8217CAA0`
                                                           (Katamari's form); 8217CAA0 is `b 0x822372B0`, XMemFree itself
  0x820FC1F8 (D3DCAPS9): not in Blur (GetDeviceCaps is not linked); left, read only by the unhooked GetDeviceCaps hook.

Each line changed gets `(blur-nx: old->new)`. Idempotent.

  python -I tools/renut_port_blur.py [--dry-run]
"""
import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu')
MAP = {
    '0x82AD8E48': '0x82B9A888',
    '0x82AD9308': '0x82B9AD48',
    '0x820FA9D0': '0x82020D38',
    '0x820FAEB4': '0x82020CC0',
    '0x82AD93F8': '0x82B9AE90',
    '0x82000750': '0x820008A4',
    '0x8315DFE0': '0x82C82B4C',
    '0x8315E830': '0x82C82B58',
    '__imp__sub_824C1C10': '__imp__sub_829BA5D8',
    '__imp__sub_824CAB98': '__imp__sub_8217CAA0',
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    n = 0
    for dp, _, fs in os.walk(SRC):
        for f in fs:
            if not f.endswith(('.cpp', '.h', '.inc')):
                continue
            p = os.path.join(dp, f)
            t = open(p, encoding='utf-8', errors='surrogateescape', newline='').read()
            lines = t.split('\n')
            dirty = False
            for i, line in enumerate(lines):
                code, sep, comment = line.partition('//')
                notes = []
                for old, new in MAP.items():
                    rx = re.compile(re.escape(old) + r'(?![0-9A-Za-z_])')
                    if rx.search(code):
                        code = rx.sub(new, code)
                        notes.append(f'{old}->{new}')
                if notes:
                    cr = '\r' if line.endswith('\r') else ''
                    tail = (comment.rstrip('\r') + ' ' if sep else ' // ') + '(blur-nx: ' + ', '.join(notes) + ')'
                    lines[i] = code + (sep if sep else '') + (tail if sep else tail[4:] if False else '') if False else \
                        (code + sep + comment.rstrip('\r') + ' (blur-nx: ' + ', '.join(notes) + ')' + cr if sep else
                         code.rstrip('\r') + '  // blur-nx: ' + ', '.join(notes) + cr)
                    dirty = True
                    n += 1
                    print(f'{os.path.relpath(p, ROOT)}:{i + 1}: {", ".join(notes)}')
            if dirty and not a.dry_run:
                open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write('\n'.join(lines))
    print(n, 'lines')


if __name__ == '__main__':
    main()
