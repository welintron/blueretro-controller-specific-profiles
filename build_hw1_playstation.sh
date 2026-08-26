#!/usr/bin/env bash
set -euo pipefail
IMAGE="ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER="blueretro-build-playstation$$"
OUT="$PROJECT_DIR/artifacts/playstation/BlueRetro_hw1_playstation.bin"
cleanup(){ docker rm -f "$CONTAINER" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM
mkdir -p "$PROJECT_DIR/artifacts/playstation"
docker version >/dev/null
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then docker pull "$IMAGE"; fi
docker create --entrypoint /bin/bash --name "$CONTAINER" "$IMAGE" -lc "sleep infinity" >/dev/null
docker start "$CONTAINER" >/dev/null
docker cp "$PROJECT_DIR/." "$CONTAINER:/work"
docker exec "$CONTAINER" /bin/bash -lc '
set -euo pipefail
cd /work
rm -rf build
rm -rf components/queue_bss/liblfds
git clone --depth 1 https://github.com/darthcloud/liblfds7.1.1.git components/queue_bss/liblfds
git config --global --add safe.directory /opt/esp/idf
git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread
cp configs/hw1/playstation sdkconfig
find /work \( -path /work/.git -o -path /work/build -o -path /work/artifacts \) -prune -o -type f -exec touch {} +
. "$IDF_PATH/export.sh"
export BR_HW=_hw1
export BR_SYS=_playstation
idf.py build
'
BUILD_BIN="$(docker exec "$CONTAINER" /bin/bash -lc 'find /work/build -maxdepth 1 -type f -name "BlueRetro_hw1_*.bin" -print -quit')"
test -n "$BUILD_BIN"
docker cp "$CONTAINER:$BUILD_BIN" "$OUT"
echo "Build complete: $OUT"
