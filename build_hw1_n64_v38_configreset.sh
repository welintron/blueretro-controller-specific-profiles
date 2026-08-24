#!/usr/bin/env bash
set -euo pipefail

IMAGE="ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER="blueretro-build-hw1-n64-v38-$$"
OUT="$PROJECT_DIR/artifacts/n64/BlueRetro_hw1_n64_v38_configreset.bin"

cleanup() { docker rm -f "$CONTAINER" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

echo "[1/7] Verifying Docker..."
docker version >/dev/null

echo "[2/7] Pulling ESP-IDF image..."
docker pull "$IMAGE"

echo "[3/7] Creating build container..."
docker create --entrypoint /bin/bash --name "$CONTAINER" "$IMAGE" -lc "sleep infinity" >/dev/null
docker start "$CONTAINER" >/dev/null

echo "[4/7] Copying V38 source..."
docker cp "$PROJECT_DIR/." "$CONTAINER:/work"

echo "[5/7] Preparing dependencies..."
docker exec "$CONTAINER" /bin/bash -lc '
set -euo pipefail
cd /work
rm -rf build components/queue_bss/liblfds
git clone --depth 1 https://github.com/darthcloud/liblfds7.1.1.git components/queue_bss/liblfds
git config --global --add safe.directory /opt/esp/idf
git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread
cp configs/hw1/n64 sdkconfig
find /work -path /work/build -prune -o -type f -exec touch {} +
. "$IDF_PATH/export.sh"
'

echo "[6/7] Building HW1 / Nintendo 64..."
docker exec "$CONTAINER" /bin/bash -lc '
set -euo pipefail
cd /work
. "$IDF_PATH/export.sh"
export BR_HW=_hw1
export BR_SYS=_n64
idf.py build
'

echo "[7/7] Copying firmware artifacts..."
mkdir -p "$PROJECT_DIR/artifacts/n64"
docker cp "$CONTAINER:/work/build/BlueRetro_hw1_n64.bin" "$OUT"
docker cp "$CONTAINER:/work/build/bootloader/bootloader.bin" "$PROJECT_DIR/artifacts/n64/bootloader.bin"
docker cp "$CONTAINER:/work/build/partition_table/partition-table.bin" "$PROJECT_DIR/artifacts/n64/partition-table.bin"
docker cp "$CONTAINER:/work/build/ota_data_initial.bin" "$PROJECT_DIR/artifacts/n64/ota_data_initial.bin"

echo
echo "=============================================="
echo "BlueRetro HW1 / Nintendo 64 V38 build completed"
echo "=============================================="
echo "Firmware:"
echo "  $OUT"
echo
echo "Bootloader:"
echo "  artifacts/n64/bootloader.bin"
echo
echo "Partition table:"
echo "  artifacts/n64/partition-table.bin"
echo
echo "OTA data:"
echo "  artifacts/n64/ota_data_initial.bin"
