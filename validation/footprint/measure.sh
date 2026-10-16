#!/usr/bin/env bash
# Measures the reference images of F-1, built with the cortex-m4, cortex-m7, cortex-m33 and cortex-m4-lto presets in
# MinSizeRel, and writes the F-1 fragment and the attribution of every preset: validation/footprint/measure.sh <output
# directory>, with cortex-memory-budget on PATH
set -euo pipefail

[ $# -eq 1 ] || { echo "usage: $0 <output directory>" >&2; exit 2; }
output=$(realpath -m "$1")
cd "$(dirname "$0")/../.."
rm -rf "$output/reports" "$output/fragments" "$output/attribution"

for preset in cortex-m4 cortex-m7 cortex-m33 cortex-m4-lto; do
    core=${preset#cortex-}
    core=${core%-lto}
    images=build/$preset/validation/footprint/MinSizeRel
    if [ ! -f "$images/validation.footprint.empty.elf" ]; then
        echo "measure.sh: no images in $images; build them with cmake --build --preset $preset-MinSizeRel --target validation.footprint" >&2
        exit 1
    fi
    PYTHONPATH=validation/host python3 -m emfs_validation.attribution --maps "$images" --core "$preset" --output "$output/attribution/$preset.md"
    for elf in "$images"/validation.footprint.*.elf; do
        image=$(basename "$elf" .elf)
        cortex-memory-budget validation/footprint/memory_budget.json --elf "$elf" --baseline-elf "$images/validation.footprint.empty.elf" \
            --target "$preset" --build-config MinSizeRel --cortex "$core" --linker-script validation/footprint/Footprint.ld \
            --no-dwarf --output-dir "$output/reports/$preset/${image#validation.footprint.}" > /dev/null
    done
done

compiler=$(arm-none-eabi-readelf -p .comment build/cortex-m4/validation/footprint/MinSizeRel/validation.footprint.empty.elf | sed -n 's/.*GCC: //p' | head -1)
PYTHONPATH=validation/host python3 -m emfs_validation.footprint --reports "$output/reports" \
    --toolchain "arm-none-eabi-gcc $compiler" --configuration MinSizeRel --output "$output/fragments/footprint.json"
