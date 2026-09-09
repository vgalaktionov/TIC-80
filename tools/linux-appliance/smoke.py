"""Exercise the actual ARM64 SDL/GL frontend under Xvfb, not the Pi driver."""
import os
from pathlib import Path
import struct
import subprocess
import time

from PIL import Image, ImageChops

OUT = Path('/tmp/tic80-smoke')
OUT.mkdir(exist_ok=True)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=10)


def capture(name):
    path = OUT / (name + '.xwd')
    run('xwd', '-silent', '-root', '-out', str(path))
    data = path.read_bytes()
    header = struct.unpack('>25I', data[:100])
    size, version, _, depth, width, height = header[:6]
    assert version == 7 and depth == 24
    assert header[7] == 0 and header[11] == 32, header
    assert header[14:17] == (0xff0000, 0xff00, 0xff)
    offset = size + header[19] * 12
    image = Image.frombytes('RGB', (width, height), data[offset:], 'raw', 'BGRX', header[12])
    image.save(OUT / (name + '.png'))
    center = image.crop((200, 120, 1080, 600))
    colors = center.getcolors(center.width * center.height)
    assert len(colors) > 4, f'{name}: blank or near-blank image'
    assert sum(n for n, rgb in colors if max(rgb) > 80) > 2000, f'{name}: no visible content'
    return image


env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='x11', LIBGL_ALWAYS_SOFTWARE='1')
with (OUT / 'application.log').open('w') as log:
    wm = subprocess.Popen(['openbox', '--config-file', '/usr/local/lib/tic80/openbox.xml'], stdout=log, stderr=log)
    app = None
    try:
        for _ in range(50):
            if 'window id' in run('xprop', '-root', '_NET_SUPPORTING_WM_CHECK'):
                break
            time.sleep(0.1)
        else:
            raise AssertionError('Window manager did not become ready')
        app = subprocess.Popen([
            '/usr/local/bin/tic80', '--fullscreen', '--skip', '--crt', '--fs=' + str(OUT),
        ], env=env, stdout=log, stderr=log)
        time.sleep(5)
        assert app.poll() is None, 'TIC-80 exited during startup'
        window = run('xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)).splitlines()[-1]
        geometry = run('xdotool', 'getwindowgeometry', '--shell', window)
        assert 'WIDTH=1280\n' in geometry and 'HEIGHT=720\n' in geometry, geometry
        before = capture('crt-on')
        run('xdotool', 'windowactivate', '--sync', window)
        run('xdotool', 'key', 'F6')
        time.sleep(2)
        after = capture('crt-off')
        changed = ImageChops.difference(before, after).convert('L')
        assert sum(changed.histogram()[8:]) > 10000, 'CRT toggle did not substantially change pixels'
        run('xdotool', 'key', 'F6')
        time.sleep(2)
        capture('crt-on-again')
        assert app.poll() is None, 'TIC-80 crashed after toggling CRT'
    finally:
        for process in (app, wm):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
errors = (OUT / 'application.log').read_text()
assert 'Failed to load' not in errors and 'Failed to link shader' not in errors, errors
print('PASS: ARM64 startup, fullscreen, visible CRT on/off/on; Xvfb software GL only.')
