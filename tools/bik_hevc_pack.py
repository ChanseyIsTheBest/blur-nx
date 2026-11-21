#!/usr/bin/env python3
"""blur-nx 1.0.1 (2026-11-21, the user: "rencode video and audio to opus and h265", "also Bink videos ZXC compressed
loading as well like sart"): makes NAME.bik.hevc from one of Blur's Bink 1 movies (BIKi), as rr6-nx's
tools/hevc_sfd_pack.py does for its Sofdec movies (credit rr6-nx; the .hevc format, the tag, the encoder settings and
the access units are its code). The port (app/src/cpu/blur_bink_hevc.cpp, sdk rex/filesystem/sfd_hevc.h) shows the game
the .bik as it was, every header, frame index and audio track byte as it was, but each frame's video data zero except
a 10-byte tag at its start, and writes the frame's H.265 picture into Bink's frame buffers where Bink would have
decoded it.

Bink 1's layout (little-endian; the frame index right after the header, frame 0 right after the index):
  0 "BIKi", 4 the file's size - 8, 8 the frame count, 12 the largest frame, 16 the frame count, 20 width, 24 height,
  28 / 32 the frame rate's numerator / denominator, 36 video flags, 40 the audio track count T, then T x 4 bytes, T x 4
  (each track's rate and flags) and T x 4 (each track's id); then (frames + 1) u32 offsets (bit 0: a keyframe).
  A frame: for each audio track a u32 size and that many bytes, then the video data to the frame's end.

usage:
  python -P tools/bik_hevc_pack.py IN.bik OUT.bik.hevc [--fraction 0.22] [--gop 120] [--encoder libx265|hevc_nvenc]
      [--ffmpeg C:/ffmpeg/ffmpeg.exe]
  python -P tools/bik_hevc_pack.py --check IN.bik OUT.bik.hevc [--psnr-every 30]
      the movie the port shows the game (every byte outside the frames' video data as in IN, each frame's tag at its
      video data's start, zeros after it) and the H.265's PSNR against ffmpeg's Bink decode, every Nth frame
The installer writes NAME.bik.hevc.lz4b (ZXC 7 blocks around it); the port reads either.
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

MAGIC = b'RXSFDH01'
TAG_BYTES = 10
TAG_MAGIC = bytes([0xA5, 0x5A, 0xC3])


def make_tag(movie_id, index):
    t = bytearray(TAG_MAGIC)
    for v in (movie_id, index):
        t += bytes([0x80 | ((v >> 14) & 0x7F), 0x80 | ((v >> 7) & 0x7F), 0x80 | (v & 0x7F)])
    t.append(0x80 | (sum(t[3:9]) & 0x7F))
    return bytes(t)


# ---- the Bink file -------------------------------------------------------------------------------------------------

def bink_video(d):
    """Each frame's video data, [(start, end, frame index)], and (width, height, fps) from the header."""
    if d[:3] != b'BIK':
        raise SystemExit('not a Bink file')
    frames, = struct.unpack_from('<I', d, 8)
    width, height, fps_num, fps_den = struct.unpack_from('<IIII', d, 20)
    tracks, = struct.unpack_from('<I', d, 40)
    index_at = 44 + 12 * tracks
    offsets = [o & ~1 for o in struct.unpack_from(f'<{frames + 1}I', d, index_at)]
    if offsets[0] != index_at + 4 * (frames + 1) or offsets[-1] > len(d):
        raise SystemExit('the frame index is not where Bink 1 has it')
    vids = []
    for i in range(frames):
        p, end = offsets[i], offsets[i + 1]
        for _ in range(tracks):
            size, = struct.unpack_from('<I', d, p)
            p += 4 + size
        if end - p < TAG_BYTES:
            raise SystemExit(f'frame {i}: {end - p} bytes of video data, too few for a tag')
        vids.append((p, end, i))
    return vids, (width, height, fps_num / fps_den)


# ---- H.265 (rr6-nx's) ----------------------------------------------------------------------------------------------

def encode(ffmpeg, src, dst, encoder, bitrate, gop):
    if encoder == 'libx265':
        enc = ['-c:v', 'libx265', '-preset', 'slow', '-b:v', str(bitrate), '-x265-params',
               f'bframes=0:keyint={gop}:min-keyint={gop}:scenecut=0:open-gop=0:repeat-headers=1:aud=1:info=0:'
               f'log-level=error']
    else:
        enc = ['-c:v', 'hevc_nvenc', '-preset', 'p7', '-tune', 'hq', '-rc', 'vbr', '-b:v', str(bitrate), '-maxrate',
               str(bitrate * 2), '-bf', '0', '-g', str(gop), '-forced-idr', '1', '-no-scenecut', '1',
               '-bsf:v', 'hevc_metadata=aud=insert']
    cmd = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', src, '-map', '0:v:0', '-an', '-fps_mode',
           'passthrough', '-pix_fmt', 'yuv420p', *enc, '-f', 'hevc', dst]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=7200)
    if p.returncode:
        raise SystemExit('ffmpeg failed: ' + p.stderr[-2000:])


def access_units(stream):
    """An Annex B H.265 stream cut at its access unit delimiters: [(bytes, keyframe)]."""
    nals = []
    p = stream.find(b'\x00\x00\x01')
    while p >= 0:
        start = p - 1 if p > 0 and stream[p - 1] == 0 else p
        nals.append((start, (stream[p + 3] >> 1) & 0x3F))
        p = stream.find(b'\x00\x00\x01', p + 3)
    aus, cur_start, key = [], None, False
    for start, typ in nals:
        if typ == 35:  # AUD
            if cur_start is not None:
                aus.append([cur_start, start, key])
            cur_start, key = start, False
        if 16 <= typ <= 21:
            key = True
    if cur_start is not None:
        aus.append([cur_start, len(stream), key])
    return [(stream[a:b], k) for a, b, k in aus]


# ---- the .hevc file (rr6-nx's format, rex/filesystem/sfd_hevc.h) ----------------------------------------------------

def build(d, vids, frames, width, height, fps):
    """Segments: the file as it is but each frame's video data: its tag (kind 2) and zeros (kind 1)."""
    segs, stored = [], bytearray()
    pos = 0
    for k, (vs, ve, _) in enumerate(vids):
        if vs > pos:
            segs.append((pos, vs - pos, 0, 0, len(stored)))
            stored += d[pos:vs]
        segs.append((vs, TAG_BYTES, 2, k, 0))
        if ve > vs + TAG_BYTES:
            segs.append((vs + TAG_BYTES, ve - vs - TAG_BYTES, 1, 0, 0))
        pos = ve
    if pos < len(d):
        segs.append((pos, len(d) - pos, 0, 0, len(stored)))
        stored += d[pos:]
    hdr = 96
    seg_off = hdr
    stored_off = seg_off + 32 * len(segs)
    pic_off = stored_off + len(stored)
    frame_tab = pic_off + 4 * len(vids)
    data_off = frame_tab + 16 * len(frames)
    out = bytearray()
    out += MAGIC + struct.pack('<IIQQQQIIQIIIIQQ', 1, len(segs), len(d), seg_off, stored_off, len(stored), len(vids), 0,
                               pic_off, width, height, len(frames), int(round(fps * 1000)), frame_tab, 0)
    assert len(out) == hdr, len(out)
    for s in segs:
        out += struct.pack('<QQIIQ', *s)
    out += stored
    out += b''.join(struct.pack('<I', idx) for _, _, idx in vids)
    pos = data_off
    for f, key in frames:
        out += struct.pack('<QII', pos, len(f), 1 if key else 0)
        pos += len(f)
    for f, _ in frames:
        out += f
    return bytes(out), segs


def parse_hevc(blob):
    if blob[:8] != MAGIC:
        raise SystemExit('not a .hevc movie')
    (ver, nseg, size, seg_off, stored_off, stored_len, npic, _, pic_off, w, h, nfr, fps, frame_tab,
     _) = struct.unpack('<IIQQQQIIQIIIIQQ', blob[8:96])
    segs = [struct.unpack('<QQIIQ', blob[seg_off + 32 * i:seg_off + 32 * i + 32]) for i in range(nseg)]
    pics = struct.unpack(f'<{npic}I', blob[pic_off:pic_off + 4 * npic])
    frames = []
    for i in range(nfr):
        o, sz, fl = struct.unpack('<QII', blob[frame_tab + 16 * i:frame_tab + 16 * i + 16])
        frames.append((blob[o:o + sz], bool(fl & 1)))
    return dict(size=size, segs=segs, stored=blob[stored_off:stored_off + stored_len], pics=pics, w=w, h=h,
                frames=frames, fps=fps / 1000)


def synthesize(o, movie_id=1):
    out = bytearray(o['size'])
    for off, ln, kd, pic, src in o['segs']:
        if kd == 0:
            out[off:off + ln] = o['stored'][src:src + ln]
        elif kd == 2:
            out[off:off + ln] = make_tag(movie_id, o['pics'][pic])[src:src + ln]
    return bytes(out)


def yuv_frames(ffmpeg, path, w, h, fmt=None):
    """The decoded pictures one at a time (I420 bytes), streamed."""
    cmd = [ffmpeg, '-hide_banner', '-loglevel', 'error']
    if fmt:
        cmd += ['-f', fmt]
    cmd += ['-i', path, '-map', '0:v:0', '-fps_mode', 'passthrough', '-pix_fmt', 'yuv420p', '-f', 'rawvideo', '-']
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    fs = w * h * 3 // 2
    try:
        while True:
            b = p.stdout.read(fs)
            if len(b) < fs:
                break
            yield b
    finally:
        p.stdout.close()
        p.kill()
        p.wait()


def psnr(a, b):
    e = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return 99.0 if e == 0 else 10 * math.log10(255.0 ** 2 / e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--fraction', type=float, default=0.22)
    ap.add_argument('--gop', type=int, default=120)
    ap.add_argument('--encoder', default='libx265', choices=['libx265', 'hevc_nvenc'])
    ap.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--psnr-every', type=int, default=30)
    a = ap.parse_args()
    d = open(a.src, 'rb').read()
    vids, (width, height, fps) = bink_video(d)
    if a.check:
        return check(a, d, vids)
    video_bytes = sum(ve - vs for vs, ve, _ in vids)
    seconds = len(vids) / fps
    bitrate = int(video_bytes * 8 / seconds * a.fraction)
    with tempfile.TemporaryDirectory() as tmp:
        h265 = os.path.join(tmp, 'v.hevc')
        encode(a.ffmpeg, a.src, h265, a.encoder, bitrate, a.gop)
        frames = access_units(open(h265, 'rb').read())
    if len(frames) != len(vids):
        raise SystemExit(f'{len(frames)} H.265 frames for {len(vids)} Bink frames')
    if not frames[0][1]:
        raise SystemExit('the first H.265 frame is not a keyframe')
    blob, segs = build(d, vids, frames, width, height, fps)
    tmp = a.dst + '.part'
    open(tmp, 'wb').write(blob)
    os.replace(tmp, a.dst)
    h265_bytes = sum(len(f) for f, _ in frames)
    print(f'{os.path.basename(a.src)}: {width}x{height} {fps:.2f} fps, {len(vids)} frames, Bink video '
          f'{video_bytes / 1e6:.1f} MB at {video_bytes * 8 / seconds / 1e6:.2f} Mbit/s -> H.265 {h265_bytes / 1e6:.1f} MB '
          f'at {h265_bytes * 8 / seconds / 1e6:.2f} Mbit/s ({a.encoder}); {len(d) / 1e6:.1f} MB -> {len(blob) / 1e6:.1f} '
          f'MB, {len(segs)} segments', flush=True)


def check(a, d, vids):
    o = parse_hevc(open(a.dst, 'rb').read())
    view = synthesize(o, movie_id=77)
    if len(view) != len(d):
        raise SystemExit('the size differs')
    video_mask = np.zeros(len(d), bool)
    for vs, ve, _ in vids:
        video_mask[vs:ve] = True
    dv, vv = np.frombuffer(d, np.uint8), np.frombuffer(view, np.uint8)
    outside = np.flatnonzero((dv != vv) & ~video_mask)
    bad = sum(1 for vs, ve, idx in vids if view[vs:vs + TAG_BYTES] != make_tag(77, idx))
    nonzero = sum(1 for vs, ve, _ in vids if any(view[vs + TAG_BYTES:ve]))
    print(f'bytes outside the video data that differ: {len(outside)}; frames: {len(vids)}, tags not where they should '
          f'be: {bad}, video data not zero after the tag: {nonzero}')
    w, h = o['w'], o['h']
    with tempfile.TemporaryDirectory() as tmp:
        hv = os.path.join(tmp, 'v.hevc')
        open(hv, 'wb').write(b''.join(f for f, _ in o['frames']))
        vals, n_ref = [], 0
        ref_it, got_it = yuv_frames(a.ffmpeg, a.src, w, h), yuv_frames(a.ffmpeg, hv, w, h, 'hevc')
        for r, g in zip(ref_it, got_it):
            if n_ref % a.psnr_every == 0:
                vals.append(psnr(np.frombuffer(r, np.uint8), np.frombuffer(g, np.uint8)))
            n_ref += 1
        n_got = n_ref + sum(1 for _ in got_it)
        n_ref += sum(1 for _ in ref_it)
    print(f'frames: Bink {n_ref}, H.265 {n_got}, keyframes {sum(1 for _, k in o["frames"] if k)}')
    print(f'PSNR every {a.psnr_every}th frame: mean {np.mean(vals):.1f} dB, min {np.min(vals):.1f} dB')


if __name__ == '__main__':
    main()
