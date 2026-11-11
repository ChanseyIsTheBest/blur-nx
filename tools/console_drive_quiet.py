#!/usr/bin/env python3
"""blur-nx (2026-11-11, after sasrt-nx's drive_quiet.py, its console-remote-guide.md section 4): drive a console race
for a performance profile without disturbing it. Holds ZR (the accelerator) for S seconds, sending the press again
every 10 s (a dropped virtual controller then recovers), takes ONE screenshot at 20 s to show the race is running and
none after (sys-botbase's pixelPeek stalls the game about 250 ms: no shots in measured windows), releases ZR at the end.
Checks first that Blur is the running title (console_run.SASRT_TITLE: Blur's forwarder here).
  python tools/console_drive_quiet.py OUT [S]          (OUT: the run's folder; S: seconds, default 200)
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import console_run as cr  # noqa: E402
import switch_remote as sr  # noqa: E402


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 200.0
    os.makedirs(out, exist_ok=True)
    bot = sr.Bot(sr.host_of([]), timeout=30)
    try:
        title = cr.title_of(bot)
        if title != cr.SASRT_TITLE:
            sys.exit('Blur is not the running title (%016X): nothing sent' % title)
        start = time.time()
        bot.send('press ZR')
        print(time.strftime('%H:%M:%S'), 'ZR held for %.0f s' % seconds, flush=True)
        shot_done = False
        while time.time() - start < seconds:
            time.sleep(1.0)
            t = time.time() - start
            if not shot_done and t >= 20:
                try:
                    bot.shot(os.path.join(out, 'drive_20s.jpg'))
                    print(time.strftime('%H:%M:%S'), 'the one shot of the drive: drive_20s.jpg', flush=True)
                except (OSError, ValueError) as e:
                    print('shot failed:', e, flush=True)
                shot_done = True
            if int(t) % 10 == 0:
                bot.send('press ZR')
        bot.send('release ZR')
        print(time.strftime('%H:%M:%S'), 'ZR released', flush=True)
    finally:
        bot.close()


if __name__ == '__main__':
    main()
