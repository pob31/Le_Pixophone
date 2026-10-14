"""Measure rolling-shutter band spacing in captured or output frames.

Usage: python tools/bandmeter.py <frame.jpg|png> [...]

Finds the picture area inside a black border, takes the row profile of the
right third of the frame, removes slow gradients and reports the dominant band
period in pixels and the number of band cycles per picture height.
Reference: Documentation/reference/*.jpg measure ~15 cycles per picture height
(PS3 Eye VGA @ 30 fps against the 490 Hz PWM light table).
"""
import sys
import numpy as np
from PIL import Image

for path in sys.argv[1:]:
    img = np.asarray(Image.open(path).convert('RGB')).astype(float)
    lum = img.mean(axis=2)
    # find the picture area: rows/cols noticeably brighter than the black border
    rows = np.where(lum.mean(axis=1) > 12)[0]
    cols = np.where(lum.mean(axis=0) > 12)[0]
    r0, r1, c0, c1 = rows[0], rows[-1], cols[0], cols[-1]
    pic = img[r0:r1 + 1, c0:c1 + 1]
    h, w = pic.shape[:2]
    # use the right third of the frame (avoids the hand in image 1); max channel tracks the bands
    prof = pic[:, int(w * 0.7):, :].max(axis=2).mean(axis=1)
    prof = prof - np.convolve(prof, np.ones(61) / 61, mode='same')  # remove slow gradients
    prof = prof[30:-30] * np.hanning(len(prof) - 60)
    n = len(prof)
    spec = np.abs(np.fft.rfft(prof, n * 8))
    freqs = np.fft.rfftfreq(n * 8)  # cycles per row
    lo = np.searchsorted(freqs, 1 / 80)  # ignore periods > 80 rows
    k = lo + np.argmax(spec[lo:])
    period_rows = 1 / freqs[k]
    cycles = h / period_rows
    print(f'{path.split(chr(92))[-1].split("/")[-1]}: picture {w}x{h} at ({c0},{r0}); '
          f'band period {period_rows:.1f} px -> {cycles:.1f} cycles per picture height')
