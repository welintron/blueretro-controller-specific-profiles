#!/usr/bin/env bash
set -euo pipefail

IMAGE="ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER="blueretro-build-hw1-3do-v38-$$"
OUT="$PROJECT_DIR/artifacts/3do/BlueRetro_hw1_3do_v38_configreset.bin"

cleanup() {
    docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

docker version >/dev/null
docker pull "$IMAGE"

docker create \
    --entrypoint /bin/bash \
    --name "$CONTAINER" \
    "$IMAGE" \
    -lc "sleep infinity" >/dev/null

docker start "$CONTAINER" >/dev/null
docker cp "$PROJECT_DIR/." "$CONTAINER:/work"

docker exec "$CONTAINER" /bin/bash -lc '
set -euo pipefail
cd /work
rm -rf build components/queue_bss/liblfds
git clone --depth 1 https://github.com/darthcloud/liblfds7.1.1.git components/queue_bss/liblfds
git config --global --add safe.directory /opt/esp/idf
git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread
cp configs/hw1/3do sdkconfig
find /work -path /work/build -prune -o -type f -exec touch {} +
. "$IDF_PATH/export.sh"
export BR_HW=_hw1
export BR_SYS=_3do
idf.py build
'

mkdir -p "$PROJECT_DIR/artifacts/3do"
docker cp "$CONTAINER:/work/build/BlueRetro_hw1_3do.bin" "$OUT"
docker cp "$CONTAINER:/work/build/bootloader/bootloader.bin" \
    "$PROJECT_DIR/artifacts/3do/bootloader.bin"
docker cp "$CONTAINER:/work/build/partition_table/partition-table.bin" \
    "$PROJECT_DIR/artifacts/3do/partition-table.bin"
docker cp "$CONTAINER:/work/build/ota_data_initial.bin" \
    "$PROJECT_DIR/artifacts/3do/ota_data_initial.bin"

echo "Build complete: $OUT"
