#!/usr/bin/env python3
"""
Interactive OpenVTx RF Power PID Tuner & Step-Response Analyzer.

Works with firmware built with `-D POWER_PID_TUNING=1` (e.g. `DIY_VTX820_tune`
or `DIY_VTX820_tune_stlink`), connected either:
  1. Directly via a half-duplex USB-TTL adapter on PA9 (`--port COMx`), or
  2. Through a Betaflight flight controller USB port (`--fc --port COMx`).

Examples:
  python python/pid_tuner.py --fc
  python python/pid_tuner.py --port COM5
  python python/pid_tuner.py --fc --plot
"""

import argparse
import csv
import os
import re
import sys
import time
from datetime import datetime

import serial

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PIO_SCRIPTS_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "src", "scripts"))
if PIO_SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, PIO_SCRIPTS_DIR)

import serials_find  # noqa: E402

TUNING_BAUD = 57600


def enable_bf_passthrough(port, baud=TUNING_BAUD):
    print(f"[FC] Connecting to Betaflight CLI on {port} @ 115200...")
    with serial.Serial(port=port, baudrate=115200, timeout=1.0) as s:
        s.reset_input_buffer()
        s.write(b"#\r\n")
        time.sleep(0.4)
        banner = s.read(s.in_waiting or 256).decode("utf-8", errors="ignore")
        if "#" not in banner:
            print("[FC] CLI prompt '#' not detected (already in passthrough mode?). Continuing...")
            return

        s.reset_input_buffer()
        s.write(b"serial\r\n")
        time.sleep(0.4)
        serial_cfg = s.read(s.in_waiting or 2048).decode("utf-8", errors="ignore")

        uart_idx = None
        for line in serial_cfg.splitlines():
            m = re.search(r"serial\s+(\d+)\s+(\d+)\s+", line.strip())
            if m and m.group(2) in ("2048", "8192", "4096", "131073"):
                uart_idx = m.group(1)
                print(f"[FC] Found VTx UART index {uart_idx}: {line.strip()}")
                break

        if uart_idx is None:
            raise RuntimeError("Could not find a VTx UART (SA/TRAMP/MSP) in Betaflight `serial` config.")

        cmd = f"serialpassthrough {uart_idx} {baud}\r\n"
        print(f"[FC] Enabling passthrough: {cmd.strip()}")
        s.write(cmd.encode("ascii"))
        time.sleep(0.3)


class TunerLink:
    def __init__(self, port, baud=TUNING_BAUD, half_duplex=True):
        self.half_duplex = half_duplex
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=0.1)
        time.sleep(0.1)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_cmd(self, cmd_str):
        payload = (cmd_str.strip() + "\r\n").encode("ascii")
        self.ser.reset_input_buffer()
        self.ser.write(payload)
        self.ser.flush()
        if self.half_duplex:
            # Drain single-wire half-duplex TX echo if present
            time.sleep(max(0.015, len(payload) * 10.0 / self.ser.baudrate + 0.005))
            in_wait = self.ser.in_waiting
            if in_wait > 0:
                peek = self.ser.read(in_wait)
                if peek.startswith(payload):
                    remainder = peek[len(payload):]
                else:
                    remainder = peek
                self._leftover = getattr(self, "_leftover", b"") + remainder

    def read_lines(self, duration_s=0.4, stop_on_end=False):
        buf = getattr(self, "_leftover", b"")
        self._leftover = b""
        lines = []
        deadline = time.time() + duration_s
        while time.time() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    raw_line, buf = buf.split(b"\n", 1)
                    line = raw_line.decode("utf-8", errors="replace").strip()
                    if line:
                        lines.append(line)
                        if stop_on_end and line.startswith("# END"):
                            self._leftover = buf
                            return lines
        self._leftover = buf
        return lines


def parse_samples(lines):
    """Parse `D,ms,sp,raw,filt,pwm` lines into list of dicts."""
    samples = []
    for line in lines:
        if not line.startswith("D,"):
            continue
        parts = line.split(",")
        if len(parts) != 6:
            continue
        try:
            samples.append({
                "ms": int(parts[1]),
                "sp": int(parts[2]),
                "raw": int(parts[3]),
                "filt": float(parts[4]),
                "pwm": int(parts[5]),
            })
        except ValueError:
            continue
    return samples


def analyze_step(samples, label=""):
    if len(samples) < 5:
        print("[WARN] Too few samples captured to analyze.")
        return None

    y0 = samples[0]["filt"]
    sp = samples[-1]["sp"]
    step_mag = sp - y0
    if abs(step_mag) < 10.0:
        print(f"[INFO] Step magnitude too small (y0={y0:.1f}, sp={sp}).")
        return None

    direction = 1.0 if step_mag > 0 else -1.0
    t10 = None
    t90 = None
    for s in samples:
        prog = (s["filt"] - y0) / step_mag
        if t10 is None and prog >= 0.10:
            t10 = s["ms"]
        if t90 is None and prog >= 0.90:
            t90 = s["ms"]

    rise_ms = (t90 - t10) if (t10 is not None and t90 is not None) else None

    # Peak overshoot along step direction
    excursions = [(s["filt"] - sp) * direction for s in samples]
    max_overshoot_mv = max(0.0, max(excursions))
    overshoot_pct = (max_overshoot_mv / abs(step_mag)) * 100.0

    # Settling time within max(2% of step, 5 mV)
    band = max(abs(step_mag) * 0.02, 5.0)
    settle_ms = samples[-1]["ms"]
    for idx in range(len(samples) - 1, -1, -1):
        if abs(samples[idx]["filt"] - sp) > band:
            settle_ms = samples[min(idx + 1, len(samples) - 1)]["ms"]
            break
    else:
        settle_ms = samples[0]["ms"]

    # Steady-state stats over final 25% of capture
    tail = samples[max(1, int(len(samples) * 0.75)):]
    ss_err = sum(s["filt"] - sp for s in tail) / len(tail)
    vpd_ripple = max(s["raw"] for s in tail) - min(s["raw"] for s in tail)
    pwm_ripple = max(s["pwm"] for s in tail) - min(s["pwm"] for s in tail)

    title = f"=== Step Response {label} ({y0:.0f} -> {sp} mV) ==="
    print(title)
    print(f"  Rise time (10-90%) : {rise_ms if rise_ms is not None else 'N/A'} ms")
    print(f"  Settling time      : {settle_ms} ms (within +-{band:.1f} mV)")
    print(f"  Peak overshoot     : {max_overshoot_mv:.1f} mV ({overshoot_pct:.1f}%)")
    print(f"  Steady-state error : {ss_err:+.2f} mV")
    print(f"  Tail ripple        : VPD_raw={vpd_ripple} mV p-p, PWM={pwm_ripple} counts p-p")

    return {
        "rise_ms": rise_ms,
        "settle_ms": settle_ms,
        "overshoot_mv": max_overshoot_mv,
        "overshoot_pct": overshoot_pct,
        "ss_err": ss_err,
        "pwm_ripple": pwm_ripple,
    }


def save_csv(samples, csv_path):
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["ms", "sp", "raw", "filt", "pwm"])
        writer.writeheader()
        writer.writerows(samples)
    print(f"  Saved {len(samples)} samples -> {csv_path}")


def maybe_plot(samples, title="OpenVTx Power Step Response"):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("  [Tip] Install matplotlib (`pip install matplotlib`) to view live plots.")
        return

    t = [s["ms"] for s in samples]
    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(8, 5))
    ax1.plot(t, [s["sp"] for s in samples], "k--", label="VpdSetPoint (mV)")
    ax1.plot(t, [s["raw"] for s in samples], alpha=0.35, label="VPD Raw (mV)")
    ax1.plot(t, [s["filt"] for s in samples], linewidth=1.8, label="VPD Filtered (mV)")
    ax1.set_ylabel("VPD (mV)")
    ax1.set_title(title)
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    ax2.plot(t, [s["pwm"] for s in samples], color="tab:orange", label="PWM (2000..2500)")
    ax2.set_xlabel("Time (ms)")
    ax2.set_ylabel("PWM")
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


def run_step_test(link, from_db, to_db, csv_path=None, do_plot=False, label=""):
    print(f"\n[Step] Pre-setting {from_db} dBm and waiting 1.0s to settle...")
    link.send_cmd(f"db={from_db}")
    link.read_lines(duration_s=1.8, stop_on_end=True)
    time.sleep(0.2)

    print(f"[Step] Stepping to {to_db} dBm and capturing 1.5s response...")
    link.send_cmd(f"db={to_db}")
    lines = link.read_lines(duration_s=2.2, stop_on_end=True)
    samples = parse_samples(lines)
    metrics = analyze_step(samples, label=label or f"{from_db}->{to_db}dBm")

    if samples:
        if not csv_path:
            ts = datetime.now().strftime("%H%M%S")
            csv_path = os.path.join(SCRIPT_DIR, f"pid_step_{from_db}_to_{to_db}dBm_{ts}.csv")
        save_csv(samples, csv_path)
        if do_plot:
            maybe_plot(samples, title=f"Step {from_db} -> {to_db} dBm ({label})")
    return metrics


def print_help():
    print(
        "\nCommands:\n"
        "  ?                      Query current PID gains & live VPD/PWM status\n"
        "  p=0.05  i=0.02  d=0.0  Update Kp / Ki / Kd live in RAM\n"
        "  a=0.25  b=3.0          Update EMA alpha (0.01..1) / deadband (mV)\n"
        "  l=10    u=15           Update max PWM step down (power UP) / step up (power DOWN)\n"
        "  m=1 / m=0              Switch mode: 1 = PID, 0 = Legacy +-1 step\n"
        "  db=26   w=400  f=5800  Set power (dBm / mW) or freq (MHz) + capture 1.5s burst\n"
        "  step 20 32 [out.csv]   Automated step test (20 -> 32 dBm) + metrics & CSV\n"
        "  ab 20 32               Side-by-side A/B step test (Legacy vs PID)\n"
        "  boot=msp / boot=sa     Reboot VTx into bootloader for normal firmware flashing\n"
        "  q / exit               Quit tuner\n"
    )


def main():
    parser = argparse.ArgumentParser(description="OpenVTx Live PID Tuner")
    parser.add_argument("--port", default=None, help="Serial COM port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=TUNING_BAUD, help=f"Baud rate (default: {TUNING_BAUD})")
    parser.add_argument("--fc", action="store_true", help="Connect via Betaflight FC serialpassthrough")
    parser.add_argument("--plot", action="store_true", help="Plot step responses with matplotlib")
    args = parser.parse_args()

    port = args.port or serials_find.get_serial_port()
    if args.fc:
        enable_bf_passthrough(port, args.baud)

    link = TunerLink(port, baud=args.baud, half_duplex=True)
    print(f"[Connected] {port} @ {args.baud} 8N1. Type 'help' for commands.")
    link.send_cmd("?")
    for line in link.read_lines(0.4):
        print(" ", line)

    try:
        while True:
            try:
                raw = input("pid> ").strip()
            except EOFError:
                break
            if not raw:
                continue
            if raw in ("q", "quit", "exit"):
                break
            if raw == "help":
                print_help()
                continue

            parts = raw.split()
            if parts[0] == "step" and len(parts) >= 3:
                csv_out = parts[3] if len(parts) >= 4 else None
                run_step_test(link, parts[1], parts[2], csv_path=csv_out, do_plot=args.plot)
                continue

            if parts[0] == "ab" and len(parts) >= 3:
                link.send_cmd("m=0")
                link.read_lines(0.3)
                m_leg = run_step_test(link, parts[1], parts[2], do_plot=args.plot, label="Legacy +-1")
                link.send_cmd("m=1")
                link.read_lines(0.3)
                m_pid = run_step_test(link, parts[1], parts[2], do_plot=args.plot, label="I-PD PID")
                if m_leg and m_pid:
                    print("\n=== A/B Summary ===")
                    print(f"  Legacy : rise={m_leg['rise_ms']} ms, settle={m_leg['settle_ms']} ms, overshoot={m_leg['overshoot_pct']:.1f}%")
                    print(f"  PID    : rise={m_pid['rise_ms']} ms, settle={m_pid['settle_ms']} ms, overshoot={m_pid['overshoot_pct']:.1f}%")
                continue

            link.send_cmd(raw)
            wait_s = 2.2 if raw.startswith(("db", "w", "s", "f")) else 0.4
            lines = link.read_lines(duration_s=wait_s, stop_on_end=True)
            samples = parse_samples(lines)
            for line in lines:
                if not line.startswith("D,"):
                    print(" ", line)
            if samples:
                analyze_step(samples)
    finally:
        link.close()


if __name__ == "__main__":
    main()
