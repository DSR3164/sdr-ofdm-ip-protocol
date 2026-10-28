#!/usr/bin/env python3
import argparse
import json
import os
import re
import statistics
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Parse and report iperf3 SDR speed test results.")
    parser.add_argument(
        "--json",
        default=os.environ.get("JSON_OUT", "/tmp/iperf_client_ci.json"),
        help="Path to iperf3 client JSON output file",
    )
    parser.add_argument(
        "--log-b",
        default=os.environ.get("LOG_B", ""),
        help="Path to Node B log file for DSP telemetry",
    )
    parser.add_argument(
        "--dsp-telemetry",
        dest="dsp_telemetry",
        action="store_true",
        default=None,
        help="Force enable DSP telemetry parsing",
    )
    parser.add_argument(
        "--no-dsp-telemetry",
        dest="dsp_telemetry",
        action="store_false",
        help="Disable DSP telemetry parsing",
    )
    parser.add_argument(
        "--min-speed",
        type=float,
        default=float(os.environ.get("MIN_SPEED_MBPS", "9.5")),
        help="Minimum required speed in Mbps (default: 9.5)",
    )
    parser.add_argument(
        "--max-loss",
        type=float,
        default=float(os.environ.get("MAX_PACKET_LOSS", "45.0")),
        help="Maximum allowed packet loss in percent (default: 45.0)",
    )
    parser.add_argument(
        "--channel-cap",
        type=float,
        default=float(os.environ.get("MAX_PHYSICAL_MBPS", "12.0")),
        help="Physical channel capacity in Mbps (default: 12.0)",
    )
    parser.add_argument(
        "--target-bitrate",
        default=os.environ.get("TARGET_BITRATE", os.environ.get("TEST_BITRATE", "12M")),
        help="Target bitrate string (default: 12M)",
    )
    parser.add_argument(
        "--restore-tty",
        action="store_true",
        default=False,
        help="Restore terminal line discipline via stty before output",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    if args.restore_tty:
        try:
            os.system("stty sane opost onlcr icrnl < /dev/tty 2>/dev/null || stty sane 2>/dev/null")
        except Exception:
            pass

    # Read DSP telemetry from log_b if available and enabled
    dsp_info = {}
    collect_dsp = args.dsp_telemetry if args.dsp_telemetry is not None else bool(args.log_b)
    if collect_dsp and args.log_b and os.path.exists(args.log_b):
        try:
            with open(args.log_b) as lf:
                for line in lf:
                    if "[RX Final Stats]" in line or "[RX Stats]" in line:
                        m_cfo = re.search(r"CFO:\s*(?:mean=)?([+-]?\d+(?:\.\d+)?)\s*Hz", line)
                        if m_cfo:
                            dsp_info["cfo"] = float(m_cfo.group(1))
                        m_drops = re.search(r"Drops\s*\(([^)]+)\)", line)
                        if m_drops:
                            dsp_info["drops"] = m_drops.group(1).replace(" ", "")
                        m_dec = re.search(r"Decoded:\s*(\d+)", line)
                        if m_dec:
                            dsp_info["decoded"] = int(m_dec.group(1))
                        m_cp = re.search(r"CP:\s*(\d+)", line)
                        if m_cp:
                            dsp_info["cp"] = int(m_cp.group(1))
                        m_zc = re.search(r"ZC:\s*(\d+)", line)
                        if m_zc:
                            dsp_info["zc"] = int(m_zc.group(1))
                        m_zcm = re.search(r"ZC_miss:\s*(\d+)", line)
                        if m_zcm:
                            dsp_info["zc_miss"] = int(m_zcm.group(1))
        except Exception:
            pass

    # Read iperf3 JSON
    try:
        with open(args.json) as f:
            data = json.load(f)
    except Exception as e:
        print(f"❌ Ошибка чтения JSON iperf3 ({args.json}): {e}")
        try:
            with open(args.json) as f:
                print("Содержимое вывода:")
                print(f.read())
        except Exception:
            pass
        sys.exit(1)

    err_msg = data.get("error")
    if err_msg:
        print("❌ iperf3 вернул ошибку: " + str(err_msg))
        sys.exit(1)

    intervals = data.get("intervals", [])
    sum_data = data.get("end", {}).get("sum", {})

    tx_rate = sum_data.get("bits_per_second", 0) / 1e6
    if tx_rate <= 0:
        tx_rate = float(args.target_bitrate.rstrip("MkmGKb"))

    total_lost = sum_data.get("lost_packets", 0)
    total_pkts = sum_data.get("packets", 0)
    overall_loss_pct = sum_data.get("lost_percent", 0.0)
    avg_jitter = sum_data.get("jitter_ms", 0.0)

    test_intervals = [it for it in intervals if not it["sum"].get("omitted", False)]
    rates = [it["sum"]["bits_per_second"] / 1e6 for it in test_intervals if it["sum"].get("bits_per_second")]
    rx_bytes_total = sum(it["sum"]["bytes"] for it in test_intervals)
    total_duration = sum(it["sum"].get("seconds", 1.0) for it in test_intervals)
    rx_avg_rate = (rx_bytes_total * 8) / (total_duration * 1e6) if total_duration > 0 else 0.0

    min_rate = min(rates) if rates else rx_avg_rate
    max_rate = max(rates) if rates else rx_avg_rate
    std_dev = statistics.stdev(rates) if len(rates) > 1 else 0.0

    channel_cap = args.channel_cap
    saturation_loss_pct = max(0.0, (tx_rate - channel_cap) / tx_rate * 100.0) if tx_rate > channel_cap else 0.0
    radio_loss_pct = max(0.0, overall_loss_pct - saturation_loss_pct)

    min_speed = args.min_speed
    max_loss = args.max_loss

    # ── оформление ──────────────────────────────────────────────────────────
    use_color = "NO_COLOR" not in os.environ

    def c(text, code):
        return f"\033[{code}m{text}\033[0m" if use_color else text

    bold, dim, red, green, yellow, cyan = "1", "2", "31", "32", "33", "36"

    w_cols = [13, 10, 14, 10, 30]
    inner = sum(w + 2 for w in w_cols) + len(w_cols) - 1

    def border(l, m, r):
        return l + m.join("─" * (w + 2) for w in w_cols) + r

    def row(cells):
        out = []
        for (text, color, align), w in zip(cells, w_cols):
            padded = f"{text:{align}{w}}"
            out.append(" " + (c(padded, color) if color else padded) + " ")
        return "│" + "│".join(out) + "│"

    def box_line(*segments):
        plain_len = sum(len(t) for t, _ in segments)
        body = "".join(c(t, col) if col else t for t, col in segments)
        return "│ " + body + " " * max(0, inner - 2 - plain_len) + " │"

    def title(text):
        return "│" + c(f"{text:^{inner}}", bold + ";" + cyan) + "│"

    # ── таблица интервалов ──────────────────────────────────────────────────
    print()
    print("┌" + "─" * inner + "┐")
    print(title("UDP · РЕЗУЛЬТАТЫ ТЕСТА"))
    print(border("├", "┬", "┤"))
    print(row([(h, bold, "^") for h in ("Интервал", "Принято", "Скорость RX", "Jitter", "Потери (PL)")]))
    print(border("├", "┼", "┤"))

    for it in intervals:
        s = it["sum"]
        start_t = s.get("start", 0.0)
        end_t = s.get("end", 0.0)
        mbytes = s.get("bytes", 0) / (1024.0 * 1024.0)
        mbps = s.get("bits_per_second", 0) / 1e6
        jit = s.get("jitter_ms", 0.0)
        lost_p = s.get("lost_packets", 0)
        tot_p = s.get("packets", 0)
        loss_pct = s.get("lost_percent", 0.0)
        omitted = s.get("omitted", False)

        interval_label = f"{start_t:4.1f}–{end_t:4.1f} с"
        if omitted:
            loss_color = dim
            pl_text = f"{lost_p}/{tot_p} ({loss_pct:.1f}%) [прогрев]"
        else:
            loss_color = green if lost_p == 0 else yellow
            pl_text = f"{lost_p}/{tot_p} ({loss_pct:.1f}%)"

        print(
            row(
                [
                    (interval_label, dim if omitted else None, "^"),
                    (f"{mbytes:.2f} МБ", dim if omitted else None, ">"),
                    (f"{mbps:.2f} Мбит/с", dim if omitted else None, ">"),
                    (f"{jit:.3f} мс", dim if omitted else None, ">"),
                    (pl_text, loss_color, ">"),
                ]
            )
        )

    print(border("├", "┴", "┤"))

    # ── сводка ──────────────────────────────────────────────────────────────
    print(title("ИТОГО"))
    print("├" + "─" * inner + "┤")

    print(box_line(("Отправка UDP (TX target):  ", dim), (f"{tx_rate:.2f} Мбит/с", bold)))

    print(
        box_line(
            ("Реальный приём (Goodput):  ", dim),
            (f"{rx_avg_rate:.2f} Мбит/с", bold + ";" + green),
            (f"  (мин: {min_speed:.1f} Мбит/с)", dim),
        )
    )
    print(box_line(("  └─ ", dim), (f"Мин {min_rate:.2f} │ Макс {max_rate:.2f} │ Разброс ±{std_dev:.2f}", None)))

    print(box_line(("Средний Jitter:            ", dim), (f"{avg_jitter:.3f} мс", bold)))

    loss_color = green if total_lost == 0 else red
    print(
        box_line(
            ("Общие потери (Total PL):   ", dim),
            (f"{overall_loss_pct:.2f}%", bold + ";" + loss_color),
            (f" ({total_lost}/{total_pkts} пакетов)", None),
        )
    )
    print(box_line(("  ├─ ", dim), (f"Макс. потери: {max_loss:.1f}%", None)))
    print(box_line(("  ├─ ", dim), (f"От перегрузки (Rate > {channel_cap:.1f}M): ~{saturation_loss_pct:.2f}%", None)))

    has_dsp = bool(dsp_info.get("cfo") is not None or "drops" in dsp_info)
    radio_prefix = "  ├─ " if has_dsp else "  └─ "
    print(box_line((radio_prefix, dim), (f"Чистые потери радиоканала:    ~{radio_loss_pct:.2f}%", None)))

    cfo_val = dsp_info.get("cfo")
    if cfo_val is not None:
        cfo_prefix = "  ├─ " if "drops" in dsp_info else "  └─ "
        print(
            box_line(
                (cfo_prefix, dim),
                (f"DSP CFO (средний за запуск):  {cfo_val:+.1f} Гц ({cfo_val/1e3:+.2f} кГц)", bold),
            )
        )
    if "drops" in dsp_info:
        dec = dsp_info.get("decoded", 0)
        cp = dsp_info.get("cp", 0)
        print(box_line(("  └─ ", dim), (f"DSP кадры (декод/CP):         {dec}/{cp}", None)))

    print("└" + "─" * inner + "┘")

    failed = False
    if rx_avg_rate < min_speed:
        print(f"❌ ПРОВАЛ: Реальная скорость {rx_avg_rate:.2f} Мбит/с ниже порога {min_speed:.1f} Мбит/с!")
        failed = True

    if overall_loss_pct > max_loss:
        print(f"❌ ПРОВАЛ: Потери пакетов {overall_loss_pct:.2f}% выше допустимого порога {max_loss:.1f}%!")
        failed = True

    if failed:
        sys.exit(1)

    print("✅ Тест скорости радиоканала успешно пройден!")


if __name__ == "__main__":
    main()
