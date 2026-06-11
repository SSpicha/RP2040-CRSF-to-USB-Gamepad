"""Conventions verified: snapshot core invariants."""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class SharedData:
    channels: tuple[int, ...]
    generation: int = 0
    packet_count: int = 0
    link: bool = False


class SnapshotSync:
    def __init__(self) -> None:
        self._shared = SharedData(channels=tuple([0] * 16))
        self._writing = threading.Lock()
        self._next_generation = 1

    def publish(self, snapshot: SharedData) -> None:
        with self._writing:
            generation = self._next_generation
            self._next_generation += 1
            self._shared = SharedData(
                channels=tuple(snapshot.channels),
                generation=generation,
                packet_count=snapshot.packet_count,
                link=snapshot.link,
            )

    def snapshotSharedDataCopyTo(self) -> SharedData:
        return self._shared

    def snapshotSharedDataCopyFrom(self, timeout_s: float = 0.05) -> SharedData | None:
        deadline = time.monotonic() + timeout_s
        while self._writing.locked():
            time.sleep(0)
            if time.monotonic() > deadline:
                return None
        return self.snapshotSharedDataCopyTo()


def run() -> int:
    failures = []
    sync = SnapshotSync()

    base = SharedData(channels=tuple([i for i in range(16)]), packet_count=10, link=True)
    sync.publish(base)

    snap1 = sync.snapshotSharedDataCopyFrom()
    if snap1 is None or snap1.generation != 1 or snap1.link is not True:
        failures.append("first snapshot invalid")

    snap_stale = sync.snapshotSharedDataCopyFrom()
    if snap_stale is None or snap_stale.generation != 1:
        failures.append("stale snapshot drift")

    sync.publish(SharedData(channels=tuple([i + 100 for i in range(16)]), packet_count=11, link=True))
    snap_after = sync.snapshotSharedDataCopyFrom()
    if snap_after is None or snap_after.packet_count != 11 or snap_after.generation != 2:
        failures.append("post-publish snapshot invalid")

    if snap1.generation == snap_after.generation:
        failures.append("generation did not advance")
    if any(snap1.channels[i] == snap_after.channels[i] for i in range(16)):
        failures.append("channels did not change")

    sync._writing.acquire()
    try:
        timed_out = sync.snapshotSharedDataCopyFrom(timeout_s=0.01) is None
    finally:
        sync._writing.release()

    if not timed_out:
        failures.append("bounded timeout did not trigger while writing")

    recovery = sync.snapshotSharedDataCopyFrom()
    if recovery is None or recovery.generation != 2:
        failures.append("post-write recovery invalid")

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1

    print("ok: snapshot sync invariants hold")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
