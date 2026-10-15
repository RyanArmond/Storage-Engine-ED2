#!/usr/bin/env bash
set -euo pipefail

BIN="./engine"
TEST_DIR="tests/tmp_test_db"
INPUT_FILE="tests/tmp_input.jsonl"
OUTPUT_FILE="tests/tmp_output.jsonl"

cleanup() {
    rm -rf "$TEST_DIR" "$INPUT_FILE" "$OUTPUT_FILE" "tests/tmp_*"
}
trap cleanup EXIT

echo "=================================================="
echo "      RUNNING PERSISTENT STORAGE ENGINE TESTS     "
echo "=================================================="

# Test 1: Describe command
echo "[Test 1] Testing 'describe'..."
$BIN describe > /dev/null
echo "  -> PASS: 'describe' exited successfully."

# Test 2: Init command
echo "[Test 2] Testing 'init'..."
rm -rf "$TEST_DIR"
$BIN init --data-dir "$TEST_DIR"
if [ ! -f "$TEST_DIR/data.log" ]; then
    echo "FAIL: $TEST_DIR/data.log was not created!"
    exit 1
fi
echo "  -> PASS: 'init' created directory and data.log."

# Test 3: Workload Execution (PUT, GET, DELETE)
echo "[Test 3] Testing basic workload run..."
cat << 'EOF' > "$INPUT_FILE"
{"id": 1, "op": "put", "key": 91, "value": "abc"}
{"id": 2, "op": "get", "key": 91}
{"id": 3, "op": "get", "key": 999}
{"id": 4, "op": "put", "key": 91, "value": "new_abc"}
{"id": 5, "op": "get", "key": 91}
{"id": 6, "op": "delete", "key": 91}
{"id": 7, "op": "get", "key": 91}
{"id": 8, "op": "delete", "key": 999}
EOF

$BIN run --data-dir "$TEST_DIR" --input "$INPUT_FILE" --output "$OUTPUT_FILE"

cat << 'EOF' > tests/expected_1.jsonl
{"id": 1, "status": "ok"}
{"id": 2, "status": "ok", "value": "abc"}
{"id": 3, "status": "not_found"}
{"id": 4, "status": "ok"}
{"id": 5, "status": "ok", "value": "new_abc"}
{"id": 6, "status": "ok"}
{"id": 7, "status": "not_found"}
{"id": 8, "status": "not_found"}
EOF

diff -u "$OUTPUT_FILE" tests/expected_1.jsonl
echo "  -> PASS: Workload output matched expected JSON Lines exactly."

# Test 4: Persistence across restart (Crash Recovery)
echo "[Test 4] Testing state persistence across restart..."
cat << 'EOF' > "$INPUT_FILE"
{"id": 10, "op": "put", "key": 100, "value": "persistent_val_1"}
{"id": 11, "op": "put", "key": 200, "value": "persistent_val_2"}
EOF
$BIN run --data-dir "$TEST_DIR" --input "$INPUT_FILE" --output "$OUTPUT_FILE"

# New run in fresh process without init to verify in-memory index is reconstructed from data.log
cat << 'EOF' > "$INPUT_FILE"
{"id": 20, "op": "get", "key": 100}
{"id": 21, "op": "get", "key": 200}
{"id": 22, "op": "get", "key": 91}
EOF
$BIN run --data-dir "$TEST_DIR" --input "$INPUT_FILE" --output "$OUTPUT_FILE"

cat << 'EOF' > tests/expected_2.jsonl
{"id": 20, "status": "ok", "value": "persistent_val_1"}
{"id": 21, "status": "ok", "value": "persistent_val_2"}
{"id": 22, "status": "not_found"}
EOF

diff -u "$OUTPUT_FILE" tests/expected_2.jsonl
echo "  -> PASS: State persisted and was accurately recovered from log."

# Test 5: Verify command
echo "[Test 5] Testing 'verify'..."
$BIN verify --data-dir "$TEST_DIR"
echo "  -> PASS: 'verify' passed on healthy database."

# Test 6: Crash / Torn Write Recovery
echo "[Test 6] Testing torn write / crash corruption recovery..."
# Append corrupted partial bytes to simulate crash mid-write
echo -ne "\xED\x01\x12\x34" >> "$TEST_DIR/data.log"

# Running a get operation should recover cleanly and ignore/truncate the torn write
cat << 'EOF' > "$INPUT_FILE"
{"id": 30, "op": "get", "key": 100}
{"id": 31, "op": "put", "key": 300, "value": "after_crash"}
{"id": 32, "op": "get", "key": 300}
EOF
$BIN run --data-dir "$TEST_DIR" --input "$INPUT_FILE" --output "$OUTPUT_FILE"

cat << 'EOF' > tests/expected_3.jsonl
{"id": 30, "status": "ok", "value": "persistent_val_1"}
{"id": 31, "status": "ok"}
{"id": 32, "status": "ok", "value": "after_crash"}
EOF

diff -u "$OUTPUT_FILE" tests/expected_3.jsonl
echo "  -> PASS: Engine auto-recovered from torn write and continued normal operation."

# Test 7: Verify after recovery
echo "[Test 7] Verifying database integrity after crash recovery..."
$BIN verify --data-dir "$TEST_DIR"
echo "  -> PASS: Database integrity intact after recovery."

# Test 8: Large keys (uint64) and string escaping
echo "[Test 8] Testing 64-bit unsigned keys and JSON string escaping..."
cat << 'EOF' > "$INPUT_FILE"
{"id": 40, "op": "put", "key": 18446744073709551615, "value": "hello \"world\"\nline2\ttab"}
{"id": 41, "op": "get", "key": 18446744073709551615}
EOF
$BIN run --data-dir "$TEST_DIR" --input "$INPUT_FILE" --output "$OUTPUT_FILE"

cat << 'EOF' > tests/expected_4.jsonl
{"id": 40, "status": "ok"}
{"id": 41, "status": "ok", "value": "hello \"world\"\nline2\ttab"}
EOF

diff -u "$OUTPUT_FILE" tests/expected_4.jsonl
echo "  -> PASS: 64-bit keys and escaped JSON strings handled correctly."

echo "=================================================="
echo "            ALL TESTS PASSED SUCCESSFULLY!        "
echo "=================================================="
