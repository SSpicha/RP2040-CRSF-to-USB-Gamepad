"""Host-side batch map command checks."""
from __future__ import annotations


def _read_int_from_json(json: str, key: str, start: int):
    key_quoted = f'"{key}"'
    key_pos = json.find(key_quoted, start)
    if key_pos < 0:
        return None, None, None
    colon = json.find(':', key_pos + len(key_quoted))
    if colon < 0:
        return None, None, None
    num_start = colon + 1
    while num_start < len(json) and json[num_start] in (' ', '\t'):
        num_start += 1
    num_end = num_start
    negative = False
    if num_end < len(json) and json[num_end] == '-':
        negative = True
        num_end += 1
    while num_end < len(json) and '0' <= json[num_end] <= '9':
        num_end += 1
    if num_end == num_start:
        return None, None, None
    value = int(json[num_start:num_end])
    if negative:
        value = -value
    return value, num_end, None


def parse_map_json(json: str):
    axes = [0] * 6
    buttons = [0] * 16
    thresholds = [1500] * 16
    pos = 0
    for i in range(6):
        v, next_pos, _ = _read_int_from_json(json, f'a{i}', pos)
        if v is None or v < 0 or v >= 16:
            return None
        axes[i] = v
        pos = next_pos
    for i in range(16):
        b, next_pos, _ = _read_int_from_json(json, f'b{i}', pos)
        if b is None or b < 0 or b >= 16:
            return None
        buttons[i] = b
        pos = next_pos
    for i in range(16):
        t, next_pos, err = _read_int_from_json(json, f't{i}', pos)
        if t is None:
            thresholds[i] = 1500
            pos = next_pos if next_pos is not None else pos
            continue
        if t < 900 or t > 1900:
            return None
        thresholds[i] = t
        pos = next_pos
    return axes, buttons, thresholds


def run() -> int:
    failures = []

    payload = '{"a0":0,"a1":1,"a2":5,"a3":4,"a4":3,"a5":2,"b0":6,"b1":7,"b2":8,"b3":9,"b4":10,"b5":11,"b6":12,"b7":13,"b8":14,"b9":15,"b10":0,"b11":1,"b12":2,"b13":3,"b14":4,"b15":5,"t0":1500,"t15":1700}'
    parsed = parse_map_json(payload)
    if parsed is None:
        failures.append("valid map json failed to parse")
    else:
        axes, buttons, thresholds = parsed
        if axes != [0, 1, 5, 4, 3, 2] or buttons != [6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5]:
            failures.append("valid map parsing produced wrong mapping")
        if thresholds[0] != 1500 or thresholds[15] != 1700:
            failures.append("valid map parsing produced wrong thresholds")

    if parse_map_json('{"a0":0}') is not None:
        failures.append("partial map json should fail")
    if parse_map_json('{"a0":20}') is not None:
        failures.append("invalid axis value should fail")  # out of 0-15
    if parse_map_json('{"a0":0,"b0":16}') is not None:
        failures.append("invalid button value should fail")
    if parse_map_json('{"a0":0,"b0":0,"t0":500}') is not None:
        failures.append("invalid threshold value should fail")

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print("ok: batch map command behavior verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
