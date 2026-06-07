"""Compare the earliest frames of PyBoy vs our engine to locate the ~78-frame
startup offset. Saves a montage and prints when each engine first shows
non-blank content."""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.abspath("red/rom.gb")


def first_nonblank(engine, n=160):
    """Return (first_frame_with_content, list of mean-lum per frame)."""
    lums, first = [], None
    for f in range(n):
        engine.tick(1, True)
        rgb = engine.screen.ndarray[:, :, :3].astype(np.float32)
        lum = (0.299*rgb[:,:,0]+0.587*rgb[:,:,1]+0.114*rgb[:,:,2])
        lums.append(lum.mean())
        # "content" = more than one distinct shade
        if first is None and len(np.unique(lum)) > 1:
            first = f
    return first, lums


def grab(engine, frames):
    out = {}
    cur = 0
    for t in frames:
        engine.tick(t - cur, True); cur = t
        out[t] = engine.screen.ndarray[:, :, :3].copy()
    return out


def main():
    from pyboy import PyBoy
    from pyboy_shim import PyBoyShim
    grab_frames = [10, 30, 50, 70, 90, 110]

    ref = PyBoy(ROM, window="null")
    rg = grab(ref, grab_frames)
    ref.stop(save=False)
    test = PyBoyShim()
    tg = grab(test, grab_frames)
    test.stop(save=False)

    cols = [np.concatenate([rg[t], tg[t]], axis=0) for t in grab_frames]
    Image.fromarray(np.concatenate(cols, axis=1), "RGB").save("red/boot_compare.png")
    print(f"saved red/boot_compare.png top=PyBoy bottom=recompiled cols={grab_frames}")

    ref2 = PyBoy(ROM, window="null"); test2 = PyBoyShim()
    rf, _ = first_nonblank(ref2); tf, _ = first_nonblank(test2)
    ref2.stop(save=False); test2.stop(save=False)
    print(f"first non-blank frame: PyBoy={rf}  recompiled={tf}  "
          f"(delta={None if rf is None or tf is None else rf-tf})")


if __name__ == "__main__":
    main()
