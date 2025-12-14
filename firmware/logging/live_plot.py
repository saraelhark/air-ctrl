#!/usr/bin/env python3
import argparse
import os
import re
import time
from collections import deque

import matplotlib.pyplot as plt


APP_INF_RE = re.compile(r"<inf>\s+app:\s+(.*)$")


def parse_app_inf_payload(line: str) -> str | None:
    m = APP_INF_RE.search(line)
    if not m:
        return None
    return m.group(1).strip()


def is_number_row(payload: str) -> bool:
    # rows start with digits (timestamp_ns)
    return bool(payload) and (payload[0].isdigit() or payload[0] == "-")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, help="Path to RTT log file")
    ap.add_argument("--x", default="ts_ns", help="X column (default: ts_ns)")
    ap.add_argument("--normalize", default="none")
    ap.add_argument(
        "--y",
        default="temp_comp_c,hum_comp_rh,gas_raw_ohm,co2_eq_ppm,breath_voc_eq_ppm,gas_pct,iaq_acc",
        help="Comma-separated Y columns",
    )
    ap.add_argument(
        "--window",
        type=int,
        default=600,
        help="Max points to keep (0 for unlimited)",
    )
    ap.add_argument(
        "--from-start",
        action="store_true",
        help="Parse from beginning (default tails from end)",
    )
    ap.add_argument(
        "--list-cols", action="store_true", help="Print detected columns then exit"
    )
    args = ap.parse_args()

    y_cols = [c.strip() for c in args.y.split(",") if c.strip()]
    if not y_cols:
        raise SystemExit("No Y columns provided")

    y_cols_unique: list[str] = []
    y_cols_seen: set[str] = set()
    for c in y_cols:
        if c not in y_cols_seen:
            y_cols_unique.append(c)
            y_cols_seen.add(c)
    y_cols = y_cols_unique

    columns: list[str] | None = None
    col_index: dict[str, int] = {}

    max_points = None if args.window <= 0 else args.window
    x_data = deque(maxlen=max_points)
    y_data = {c: deque(maxlen=max_points) for c in y_cols}

    t0_ns: int | None = None

    groups: list[tuple[str, list[str]]] = [(c, [c]) for c in y_cols]

    plt.ion()
    fig, axes = plt.subplots(
        nrows=len(groups),
        ncols=1,
        sharex=True,
        squeeze=False,
        figsize=(11, max(2, int(1.7 * len(groups)))),
    )
    axes_list = [axes[i][0] for i in range(len(groups))]

    lines: dict[str, object] = {}
    for ax, (name, cols) in zip(axes_list, groups):
        for c in cols:
            lines[c] = ax.plot([], [], label=c)[0]
        ax.grid(True, which="both", alpha=0.3)
        ax.set_ylabel(name)
        ax.margins(x=0)
    axes_list[-1].set_xlabel("t_s" if args.x == "ts_ns" else args.x)
    fig.tight_layout()

    def refresh_plot():
        x_list = list(x_data)
        if x_list:
            max_x = max(x_list)
            min_x = x_list[0]
        else:
            max_x = 0.0
            min_x = 0.0

        for ax, (_name, cols) in zip(axes_list, groups):
            for c in cols:
                y_list = list(y_data[c])
                n = min(len(x_list), len(y_list))
                lines[c].set_data(x_list[-n:], y_list[-n:])

            ax.relim()
            ax.autoscale_view()
            if max_points is None:
                ax.set_xlim(0.0, max_x if max_x > 0.0 else 1.0)
            else:
                ax.set_xlim(min_x, max_x if max_x > min_x else (min_x + 1.0))

        fig.canvas.draw_idle()
        plt.pause(0.001)

    warned_missing: set[str] = set()
    header_buf: str | None = None

    with open(args.file, "r", errors="ignore") as f:
        if not args.from_start:
            f.seek(0, os.SEEK_END)

        while True:
            line = f.readline()
            if not line:
                refresh_plot()
                time.sleep(0.05)
                continue

            payload = parse_app_inf_payload(line)
            if payload is None:
                continue

            if (
                header_buf is not None
                and (not is_number_row(payload))
                and ("," in payload)
                and (not payload.startswith("ts_ns,"))
            ):
                if header_buf.endswith(",") or payload.startswith(","):
                    header_buf = header_buf + payload
                else:
                    header_buf = header_buf + "," + payload
                continue

            # Detect header
            if payload.startswith("ts_ns,"):
                header_buf = payload
                continue

            # Parse data row (needs header first)
            if ((columns is None) and (header_buf is None)) or (
                not is_number_row(payload)
            ):
                continue

            parts = [p.strip() for p in payload.split(",")]

            try:
                if header_buf is not None:
                    columns = [c.strip() for c in header_buf.split(",")]
                    col_index = {c: i for i, c in enumerate(columns)}
                    header_buf = None

                    if args.list_cols:
                        print("Detected columns:")
                        for c in columns:
                            print(c)
                        return

                    for c in [args.x, *y_cols]:
                        if c not in col_index and c not in warned_missing:
                            print(f"Warning: requested column '{c}' not in header")
                            warned_missing.add(c)

                if columns is None:
                    continue

                if len(parts) != len(columns):
                    continue

                # Parse x
                if args.x == "ts_ns":
                    ts_ns = int(parts[col_index["ts_ns"]])
                    if t0_ns is None:
                        t0_ns = ts_ns
                    x_val = (ts_ns - t0_ns) / 1e9  # seconds since first sample
                else:
                    x_val = float(parts[col_index[args.x]])

                row_y: dict[str, float] = {}
                for c in y_cols:
                    if c in col_index:
                        row_y[c] = float(parts[col_index[c]])
                    else:
                        row_y[c] = float("nan")

                x_data.append(x_val)
                for c in y_cols:
                    y_data[c].append(row_y[c])

                refresh_plot()
            except Exception:
                # ignore parse errors
                continue


if __name__ == "__main__":
    main()
