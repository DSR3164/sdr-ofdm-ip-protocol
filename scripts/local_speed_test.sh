#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
SOIP_BIN="$BUILD_DIR/soip"

DURATION="${TEST_DURATION:-5}"
TARGET_BITRATE="${TEST_BITRATE:-12M}"
MODULATION="${MODULATION:-QAM64}"
MIN_SPEED_MBPS="${MIN_SPEED_MBPS:-9.5}"
MAX_PACKET_LOSS="${MAX_PACKET_LOSS:-45.0}"
MAX_PHYSICAL_MBPS="${MAX_PHYSICAL_MBPS:-12.0}"

if [ ! -f "$SOIP_BIN" ]; then
    echo "❌ Ошибка: исполняемый файл $SOIP_BIN не найден. Сначала соберите проект."
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

restore_tty() {
    stty sane opost onlcr icrnl < /dev/tty 2>/dev/null || stty sane opost onlcr icrnl 2>/dev/null || true
}

echo_msg() {
    restore_tty
    printf "%b\r\n" "$*"
}

stop_processes() {
    printf "\r\n=== Остановка тестовых процессов ===\r\n"
    $SUDO killall -9 soip iperf3 2>/dev/null || true
    restore_tty
    sleep 1
}

cleanup() {
    stop_processes
    if [ -n "${TEST_TMP_DIR:-}" ] && [ -d "$TEST_TMP_DIR" ]; then
        rm -rf "$TEST_TMP_DIR" 2>/dev/null || $SUDO rm -rf "$TEST_TMP_DIR" || true
    fi
    restore_tty
}
trap cleanup EXIT

echo_msg "=== 1. Очистка старых процессов ==="
stop_processes

TEST_TMP_DIR="$(mktemp -d /tmp/soip_speed_test_XXXXXX)"
chmod 777 "$TEST_TMP_DIR"
LOG_A="$TEST_TMP_DIR/soip_a_ci.log"
LOG_B="$TEST_TMP_DIR/soip_b_ci.log"
LOG_SERVER="$TEST_TMP_DIR/iperf_server_ci.log"
JSON_OUT="$TEST_TMP_DIR/iperf_client_ci.json"

export JSON_OUT MIN_SPEED_MBPS MAX_PACKET_LOSS MAX_PHYSICAL_MBPS LOG_A LOG_B

# Проверка и создание сетевых пространств имён ns0 и ns1
for ns in ns0 ns1; do
    if ! ip netns list | grep -qw "$ns"; then
        echo_msg "Создание сетевого пространства $ns..."
        $SUDO ip netns add "$ns"
        $SUDO ip netns exec "$ns" ip link set lo up
    fi
done

echo_msg "=== 2. Запуск Node A (ns0) и Node B (ns1) с модуляцией ${MODULATION} ==="
$SUDO ip netns exec ns0 "$SOIP_BIN" -n A -m "$MODULATION" < /dev/null > "$LOG_A" 2>&1 &
PID_A=$!
sleep 1

$SUDO ip netns exec ns1 "$SOIP_BIN" -n B -m "$MODULATION" < /dev/null > "$LOG_B" 2>&1 &
PID_B=$!

echo_msg "Узлы запущены (PID A: $PID_A, PID B: $PID_B). Ожидание синхронизации линка (5 сек)..."
sleep 5

# Проверка, что узлы не упали
if ! kill -0 "$PID_A" 2>/dev/null; then
    echo_msg "❌ Ошибка: Node A упал сразу после запуска! Лог:"
    cat "$LOG_A"
    exit 1
fi

if ! kill -0 "$PID_B" 2>/dev/null; then
    echo_msg "❌ Ошибка: Node B упал сразу после запуска! Лог:"
    cat "$LOG_B"
    exit 1
fi

echo_msg "=== 3. Запуск iperf3 сервера в ns0 ==="
$SUDO ip netns exec ns0 iperf3 -s -1 < /dev/null > "$LOG_SERVER" 2>&1 &
sleep 2

echo_msg "=== 4. Запуск теста iperf3 (ns1 -> ns0, ${DURATION}c, ${TARGET_BITRATE}) ==="
$SUDO ip netns exec ns1 iperf3 -c 10.0.0.1 -u -b "$TARGET_BITRATE" -i 1 -R -t "$DURATION" -O 2 -J < /dev/null > "$JSON_OUT" 2>&1 || true

# Остановка узлов для фиксации финальной статистики DSP
$SUDO killall -2 soip 2>/dev/null || true
sleep 1
restore_tty

# Парсинг результатов с помощью Python
python3 -c '
import os, sys, json, statistics, re

try:
    os.system("stty sane opost onlcr icrnl < /dev/tty 2>/dev/null || stty sane 2>/dev/null")
except Exception:
    pass

log_b_path = os.environ.get("LOG_B", "")
dsp_info = {}
if log_b_path and os.path.exists(log_b_path):
    try:
        with open(log_b_path) as lf:
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

json_path = os.environ.get("JSON_OUT", "/tmp/iperf_client_ci.json")
try:
    with open(json_path) as f:
        data = json.load(f)
except Exception as e:
    print(f"❌ Ошибка чтения JSON iperf3 ({json_path}): {e}")
    try:
        with open(json_path) as f:
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
total_lost = sum_data.get("lost_packets", 0)
total_pkts = sum_data.get("packets", 0)
overall_loss_pct = sum_data.get("lost_percent", 0.0)
avg_jitter = sum_data.get("jitter_ms", 0.0)

test_intervals = [it for it in intervals if not it["sum"].get("omitted", False)]
rates = [it["sum"]["bits_per_second"] / 1e6 for it in test_intervals if it["sum"].get("bits_per_second")]
rx_bytes_total = sum(it["sum"]["bytes"] for it in test_intervals)
total_duration = sum_data.get("seconds", sum(it["sum"].get("seconds", 1.0) for it in test_intervals))
rx_avg_rate = (rx_bytes_total * 8) / (total_duration * 1e6) if total_duration > 0 else 0.0

min_rate = min(rates) if rates else 0.0
max_rate = max(rates) if rates else 0.0
std_dev = statistics.stdev(rates) if len(rates) > 1 else 0.0

channel_cap = float(os.environ.get("MAX_PHYSICAL_MBPS", "11.0"))
saturation_loss_pct = max(0.0, (tx_rate - channel_cap) / tx_rate * 100.0) if tx_rate > channel_cap else 0.0
radio_loss_pct = max(0.0, overall_loss_pct - saturation_loss_pct)

min_speed = float(os.environ.get("MIN_SPEED_MBPS", "6.5"))
max_loss = float(os.environ.get("MAX_PACKET_LOSS", "45.0"))

# ── оформление ──────────────────────────────────────────────────────────────
USE_COLOR = "NO_COLOR" not in os.environ

def c(text, code):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text

BOLD, DIM, RED, GREEN, YELLOW, CYAN = "1", "2", "31", "32", "33", "36"

W = [13, 10, 14, 10, 24]                  # ширины колонок
INNER = sum(w + 2 for w in W) + len(W) - 1  # внутренняя ширина рамки

def border(l, m, r):
    return l + m.join("─" * (w + 2) for w in W) + r

def row(cells):
    """cells: [(plain_text, color_or_None, align)] — паддинг считается по plain."""
    out = []
    for (text, color, align), w in zip(cells, W):
        padded = f"{text:{align}{w}}"
        out.append(" " + (c(padded, color) if color else padded) + " ")
    return "│" + "│".join(out) + "│"

def box_line(*segments):
    """segments: [(plain_text, color_or_None)] — строка внутри общей рамки."""
    plain_len = sum(len(t) for t, _ in segments)
    body = "".join(c(t, col) if col else t for t, col in segments)
    return "│ " + body + " " * max(0, INNER - 2 - plain_len) + " │"

def title(text):
    return "│" + c(f"{text:^{INNER}}", BOLD + ";" + CYAN) + "│"

# ── таблица интервалов ──────────────────────────────────────────────────────
print()
print("┌" + "─" * INNER + "┐")
print(title("UDP · РЕЗУЛЬТАТЫ ТЕСТА"))
print(border("├", "┬", "┤"))
print(row([(h, BOLD, "^") for h in
           ("Интервал", "Принято", "Скорость RX", "Jitter", "Потери (PL)")]))
print(border("├", "┼", "┤"))

for it in intervals:
    s = it["sum"]
    start_t  = s.get("start", 0.0)
    end_t    = s.get("end", 0.0)
    mbytes   = s.get("bytes", 0) / (1024.0 * 1024.0)
    mbps     = s.get("bits_per_second", 0) / 1e6
    jit      = s.get("jitter_ms", 0.0)
    lost_p   = s.get("lost_packets", 0)
    tot_p    = s.get("packets", 0)
    loss_pct = s.get("lost_percent", 0.0)
    omitted  = s.get("omitted", False)

    interval_label = f"{start_t:4.1f}–{end_t:4.1f} с"
    if omitted:
        loss_color = DIM
        pl_text = f"{lost_p}/{tot_p} ({loss_pct:.1f}%) [прогрев]"
    else:
        loss_color = GREEN if lost_p == 0 else YELLOW
        pl_text = f"{lost_p}/{tot_p} ({loss_pct:.1f}%)"

    print(row([
        (interval_label,        DIM if omitted else None, "^"),
        (f"{mbytes:.2f} МБ",    DIM if omitted else None, ">"),
        (f"{mbps:.2f} Мбит/с",  DIM if omitted else None, ">"),
        (f"{jit:.3f} мс",       DIM if omitted else None, ">"),
        (pl_text,               loss_color,               ">"),
    ]))

print(border("├", "┴", "┤"))

# ── сводка ──────────────────────────────────────────────────────────────────
print(title("ИТОГО"))
print("├" + "─" * INNER + "┤")

print(box_line(("Отправка UDP (TX target):  ", DIM),
               (f"{tx_rate:.2f} Мбит/с", BOLD)))

cfo_val = dsp_info.get("cfo")
cfo_str = f"  (CFO: {cfo_val/1e3:+.2f} кГц)" if cfo_val is not None else ""
drops_val = dsp_info.get("drops")
drops_tag = f" [Дропы: {drops_val}]" if drops_val else ""

print(box_line(("Реальный приём (Goodput):  ", DIM),
               (f"{rx_avg_rate:.2f} Мбит/с", BOLD + ";" + GREEN),
               (f"  (мин: {min_speed:.1f} Мбит/с)", DIM)))
print(box_line(("  └─ ", DIM),
               (f"Мин {min_rate:.2f} │ Макс {max_rate:.2f} │ Разброс ±{std_dev:.2f}", None)))

print(box_line(("Средний Jitter:            ", DIM),
               (f"{avg_jitter:.3f} мс", BOLD)))

loss_color = GREEN if total_lost == 0 else RED
print(box_line(("Общие потери (Total PL):   ", DIM),
               (f"{overall_loss_pct:.2f}%", BOLD + ";" + loss_color),
               (f" ({total_lost}/{total_pkts} пакетов)", None)))
print(box_line(("  ├─ ", DIM),
               (f"Макс. потери: {max_loss:.1f}%", None)))
print(box_line(("  ├─ ", DIM),
               (f"От перегрузки (Rate > {channel_cap:.1f}M): ~{saturation_loss_pct:.2f}%", None)))
print(box_line(("  ├─ ", DIM),
               (f"Чистые потери радиоканала:    ~{radio_loss_pct:.2f}%", None)))

if cfo_val is not None:
    print(box_line(("  ├─ ", DIM),
                   (f"DSP CFO (средний за запуск):  {cfo_val:+.1f} Гц ({cfo_val/1e3:+.2f} кГц)", BOLD)))
if "drops" in dsp_info:
    dec = dsp_info.get("decoded", 0)
    cp = dsp_info.get("cp", 0)
    print(box_line(("  └─ ", DIM),
                   (f"DSP кадры (декод/CP):         {dec}/{cp}", None)))

print("└" + "─" * INNER + "┘")

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
'
