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

LOG_A="/tmp/soip_a_ci.log"
LOG_B="/tmp/soip_b_ci.log"
LOG_SERVER="/tmp/iperf_server_ci.log"
JSON_OUT="/tmp/iperf_client_ci.json"
LOG_CLIENT_ERR="/tmp/iperf_client_ci.err"

cleanup() {
    echo ""
    echo "=== Остановка тестовых процессов ==="
    sudo killall -9 soip iperf3 2>/dev/null || true
    sleep 1
}
trap cleanup EXIT

echo "=== 1. Очистка старых процессов ==="
cleanup

# Проверка и создание сетевых пространств имён ns0 и ns1
for ns in ns0 ns1; do
    if ! ip netns list | grep -qw "$ns"; then
        echo "Создание сетевого пространства $ns..."
        sudo ip netns add "$ns"
        sudo ip netns exec "$ns" ip link set lo up
    fi
done

echo "=== 2. Запуск Node A (ns0) и Node B (ns1) с модуляцией ${MODULATION} ==="
sudo ip netns exec ns0 "$SOIP_BIN" -n A -m "$MODULATION" < /dev/null > "$LOG_A" 2>&1 &
PID_A=$!
sleep 1

sudo ip netns exec ns1 "$SOIP_BIN" -n B -m "$MODULATION" < /dev/null > "$LOG_B" 2>&1 &
PID_B=$!

echo "Узлы запущены (PID A: $PID_A, PID B: $PID_B). Ожидание синхронизации линка..."

LINK_READY=0
for i in $(seq 1 30); do
    if ! kill -0 "$PID_A" 2>/dev/null; then
        echo "❌ Ошибка: Node A упал во время ожидания линка! Лог:"
        cat "$LOG_A"
        exit 1
    fi

    if ! kill -0 "$PID_B" 2>/dev/null; then
        echo "❌ Ошибка: Node B упал во время ожидания линка! Лог:"
        cat "$LOG_B"
        exit 1
    fi

    if sudo ip netns exec ns1 ping -c 1 -W 1 10.0.0.1 >/dev/null 2>&1; then
        LINK_READY=1
        echo "Линк синхронизирован (${i} сек)."
        break
    fi
    sleep 1
done

if [ "$LINK_READY" -ne 1 ]; then
    echo "❌ Ошибка: таймаут синхронизации линка (10.0.0.1 недоступен из ns1)."
    echo "=== Лог Node A ==="
    cat "$LOG_A"
    echo "=== Лог Node B ==="
    cat "$LOG_B"
    exit 1
fi

echo "=== 3. Запуск iperf3 сервера в ns0 ==="
sudo ip netns exec ns0 iperf3 -s -1 < /dev/null > "$LOG_SERVER" 2>&1 &
sleep 2

echo "=== 4. Запуск теста iperf3 (ns1 -> ns0, ${DURATION}c, ${TARGET_BITRATE}) ==="
sudo ip netns exec ns1 iperf3 -c 10.0.0.1 -u -b "$TARGET_BITRATE" -i 1 -R -t "$DURATION" -O 1 -J < /dev/null > "$JSON_OUT" 2> "$LOG_CLIENT_ERR" || true

# Остановка узлов для фиксации финальной статистики DSP
sudo killall -2 soip 2>/dev/null || true
sleep 1

# Экспорт переменных окружения для парсера
export JSON_OUT MIN_SPEED_MBPS MAX_PACKET_LOSS MAX_PHYSICAL_MBPS LOG_A LOG_B TARGET_BITRATE

# Парсинг результатов с помощью Python
python3 "$SCRIPT_DIR/parse_speed_test.py"
