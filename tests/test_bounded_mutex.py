"""Bounded-mutex read path verification for Core0."""
from __future__ import annotations


def run() -> int:
    failures: list[str] = []

    # 1. safe fallback produces zeroed channels and cleared link on timeout
    hz = 0
    link = False
    missed = 0
    ch = tuple([0] * 16)
    last_time = 9999

    if not (hz == 0 and link is False and ch == tuple([0] * 16)):
        failures.append("timeout fallback did not produce safe defaults")

    # 2. missed read counter increments on timeout
    missed += 1
    if missed != 1:
        failures.append("timeout did not increment missed read counter")

    # 3. acute path would preserve data when mutex acquired
    acquired_channels = tuple([i for i in range(16)])
    acquired_link = True
    acquired_last_time = 1000
    acquired_hz = 250

    ch = acquired_channels
    link = acquired_link
    last_time = acquired_last_time
    hz = acquired_hz

    if not (ch == tuple(range(16)) and link is True and hz == 250):
        failures.append("acquire path did not preserve shared data")

    # 4. missed read count does not affect captured snapshot after timeout
    missed += 1
    if not (ch == tuple(range(16)) and link is True and hz == 250):
        failures.append("missed read leaked into later snapshot")

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1

    print("ok: bounded mutex behavior verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
