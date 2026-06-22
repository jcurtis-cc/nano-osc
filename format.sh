#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./format.sh [--check]

Formats C/C++ source files with clang-format.

Options:
  --check   verify formatting without modifying files
  -h, --help
EOF
}

mode="fix"
case "${1:-}" in
    "")
        ;;
    --check)
        mode="check"
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

clang_format="${CLANG_FORMAT:-clang-format}"

if ! command -v "$clang_format" >/dev/null 2>&1; then
    echo "error: clang-format is not available" >&2
    exit 1
fi

files=()
while IFS= read -r -d '' file; do
    files+=("$file")
done < <(
    find . \
        \( -path './.git' -o -path './build' -o -path './build-*' -o -path './tests/third_party' \) -prune \
        -o -type f \
        \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
           -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
        -print0
)

if [ "${#files[@]}" -eq 0 ]; then
    echo "No source files found."
    exit 0
fi

if [ "$mode" = "check" ]; then
    "$clang_format" --dry-run --Werror "${files[@]}"
else
    "$clang_format" -i "${files[@]}"
fi
