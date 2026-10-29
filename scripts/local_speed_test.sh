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
$SUDO ip netns exec ns1 iperf3 -c 10.0.0.1 -u -b "$TARGET_BITRATE" -i 1 -R -t "$DURATION" -O 1 -J < /dev/null > "$JSON_OUT" 2>&1 || true

# Остановка узлов для фиксации финальной статистики DSP
$SUDO killall -2 soip 2>/dev/null || true
sleep 1
restore_tty

# Парсинг результатов с помощью Python
python3 "$SCRIPT_DIR/parse_speed_test.py" --restore-tty
