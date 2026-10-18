#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
SOIP_BIN="$BUILD_DIR/soip"

DURATION="${TEST_DURATION:-15}"
TARGET_BITRATE="${TEST_BITRATE:-12M}"
MODULATION="${MODULATION:-QAM64}"
MIN_SPEED_MBPS="${MIN_SPEED_MBPS:-6.5}"
MAX_PACKET_LOSS="${MAX_PACKET_LOSS:-45.0}"
MAX_PHYSICAL_MBPS="${MAX_PHYSICAL_MBPS:-11.0}"

if [ ! -f "$SOIP_BIN" ]; then
    echo "❌ Ошибка: исполняемый файл $SOIP_BIN не найден. Сначала соберите проект."
    exit 1
fi

LOG_A="/tmp/soip_a_ci.log"
LOG_B="/tmp/soip_b_ci.log"
LOG_SERVER="/tmp/iperf_server_ci.log"
JSON_OUT="/tmp/iperf_client_ci.json"

cleanup() {
    echo ""
    echo "=== Остановка тестовых процессов ==="
    sudo killall -9 soip iperf3 2>/dev/null || true
    sleep 1
}
trap cleanup EXIT

echo "=== 1. Очистка старых процессов ==="
cleanup

echo "=== 2. Запуск Node A (ns0) и Node B (ns1) с модуляцией ${MODULATION} ==="
sudo ip netns exec ns0 "$SOIP_BIN" -n A -m "$MODULATION" > "$LOG_A" 2>&1 &
PID_A=$!
sudo ip netns exec ns1 "$SOIP_BIN" -n B -m "$MODULATION" > "$LOG_B" 2>&1 &
PID_B=$!

echo "Узлы запущены (PID A: $PID_A, PID B: $PID_B). Ожидание синхронизации линка (5 сек)..."
sleep 5

# Проверка, что узлы не упали
if ! kill -0 "$PID_A" 2>/dev/null; then
    echo "❌ Ошибка: Node A упал сразу после запуска! Лог:"
    cat "$LOG_A"
    exit 1
fi

if ! kill -0 "$PID_B" 2>/dev/null; then
    echo "❌ Ошибка: Node B упал сразу после запуска! Лог:"
    cat "$LOG_B"
    exit 1
fi

echo "=== 3. Запуск iperf3 сервера в ns0 ==="
sudo ip netns exec ns0 iperf3 -s -1 > "$LOG_SERVER" 2>&1 &
sleep 2

echo "=== 4. Запуск теста iperf3 (ns1 -> ns0, ${DURATION}c, ${TARGET_BITRATE}) ==="
sudo ip netns exec ns1 iperf3 -c 10.0.0.1 -u -b "$TARGET_BITRATE" -i 1 -R -t "$DURATION" -J > "$JSON_OUT" 2>&1 || true

# Парсинг результатов с помощью Python
python3 -c '
import os, sys, json, statistics

# BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
# SOIP_BIN="$BUILD_DIR/soip"

# DURATION="${TEST_DURATION:-15}"
# TARGET_BITRATE="${TEST_BITRATE:-12M}"
# MODULATION="${MODULATION:-QAM64}"
# MIN_SPEED_MBPS="${MIN_SPEED_MBPS:-6.5}"

json_path = "/tmp/iperf_client_ci.json"
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

rates = [it["sum"]["bits_per_second"] / 1e6 for it in intervals if it["sum"].get("bits_per_second")]
rx_bytes_total = sum(it["sum"]["bytes"] for it in intervals)
total_duration = sum_data.get("seconds", 15.0)
rx_avg_rate = (rx_bytes_total * 8) / (total_duration * 1e6) if total_duration > 0 else 0.0

min_rate = min(rates) if rates else 0.0
max_rate = max(rates) if rates else 0.0
std_dev = statistics.stdev(rates) if len(rates) > 1 else 0.0

channel_cap = float(11.0)
saturation_loss_pct = max(0.0, (tx_rate - channel_cap) / tx_rate * 100.0) if tx_rate > channel_cap else 0.0
radio_loss_pct = max(0.0, overall_loss_pct - saturation_loss_pct)

min_speed = float(9.0)
max_loss = float(45.0)

# ── оформление ──────────────────────────────────────────────────────────────
USE_COLOR = "NO_COLOR" not in os.environ

def c(text, code):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text

BOLD, DIM, RED, GREEN, YELLOW, CYAN = "1", "2", "31", "32", "33", "36"

W = [13, 10, 14, 10, 20]                  # ширины колонок
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
    return "│ " + body + " " * (INNER - 2 - plain_len) + " │"

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

    loss_color = GREEN if lost_p == 0 else YELLOW
    print(row([
        (f"{start_t:4.1f}–{end_t:4.1f} с",              None,       "^"),
        (f"{mbytes:.2f} МБ",                             None,       ">"),
        (f"{mbps:.2f} Мбит/с",                           None,       ">"),
        (f"{jit:.3f} мс",                                None,       ">"),
        (f"{lost_p}/{tot_p} ({loss_pct:.1f}%)",          loss_color, ">"),
    ]))

print(border("├", "┴", "┤"))

# ── сводка ──────────────────────────────────────────────────────────────────
print(title("ИТОГО"))
print("├" + "─" * INNER + "┤")

print(box_line(("Отправка UDP (TX target):  ", DIM),
               (f"{tx_rate:.2f} Мбит/с", BOLD)))

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
print(box_line(("  └─ ", DIM),
               (f"Чистые потери радиоканала:    ~{radio_loss_pct:.2f}%", None)))

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
