#!/usr/bin/env python3
"""Reproduce the summary of the archived user-supplied hardware log.

This checks a historical recording, not a connected board or a new firmware.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOG = ROOT / "validation/hardware/2026-09-17_range_uart_32s.log"
SUMMARY = LOG.with_suffix(".json")


def summarize():
    rows = {"RANGE_STAT": [], "RANGE_DATA": []}
    for line in LOG.read_text().splitlines():
        parts = line.split(",")
        assert parts[0] in rows
        rows[parts[0]].append(dict(part.split("=", 1) for part in parts[1:]))
    stats, data = rows["RANGE_STAT"], rows["RANGE_DATA"]
    assert len(stats) == len(data) == 33
    assert {s["fw"] for s in stats} == {"1.3.0-range"}
    assert {s["baud"] for s in stats} == {"38400"}
    errors = ["bad", "missing", "duplicate", "restarts", "backwards",
              "discarded", "frame_error", "overflow", "parity", "breaks"]
    assert all(int(row[key]) == 0 for row in stats for key in errors)
    assert all(row["state"] == "RECEIVING" for row in stats)
    assert all(row["ready"] == "1" and row["valid"] == "7" and
               row["session"] == "2/2" and row["reason"] == "00/00" for row in data)
    assert len({row["boot"] for row in data}) == 1
    assert all(int(row["tx_overruns"]) == 0 for row in data)
    for previous, current in zip(data, data[1:]):
        assert int(current["tx_ms"]) - int(previous["tx_ms"]) == 1000
        assert int(current["seq"]) - int(previous["seq"]) == 5
        assert int(current["r_ok"]) - int(previous["r_ok"]) == 10
        assert all(int(current["updates"].split("/")[i]) -
                   int(previous["updates"].split("/")[i]) == 5 for i in range(2))
    assert all(int(b["ok"]) - int(a["ok"]) == 5 for a, b in zip(stats, stats[1:]))
    return {
        "log_sha256": hashlib.sha256(LOG.read_bytes()).hexdigest(),
        "stat_rows": len(stats), "data_rows": len(data),
        "interval_ms": int(data[-1]["tx_ms"]) - int(data[0]["tx_ms"]),
        "uart_frames": int(stats[-1]["ok"]) - int(stats[0]["ok"]),
        "range_successes": [int(data[-1]["updates"].split("/")[i]) -
                            int(data[0]["updates"].split("/")[i]) for i in range(2)],
        "range_bad_first_last": [int(data[0]["r_bad"]), int(data[-1]["r_bad"])],
        "all_uart_error_counters_zero": True,
        "all_reported_measurements_valid": True,
        "tx_frame_ms": sorted({int(row["tx_frame_ms"]) for row in data}),
        "irq_us": sorted({int(row["irq_us"]) for row in data}),
        "tx_overruns": 0,
    }


if __name__ == "__main__":
    actual = summarize()
    assert actual == json.loads(SUMMARY.read_text()), "Archived summary does not match raw log"
    print("PASS archived hardware recording:", json.dumps(actual))
