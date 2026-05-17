#!/bin/bash
# Test Plan for Secure World Mission Verification
#
# This script provides a testing roadmap with multiple phases:
# Phase 1: Environment check
# Phase 2: Client library compilation test
# Phase 3: TA compilation (requires SDK)
# Phase 4: Integration test
# Phase 5: Security validation

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Secure World Mission Verification - Testing Guide        ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# ============================================================================
# PHASE 1: Environment Verification
# ============================================================================

phase_env_check() {
    echo -e "${YELLOW}[PHASE 1] Environment Verification${NC}"
    echo ""

    local pass=0
    local fail=0

    # Check OP-TEE devices
    echo -n "  ✓ OP-TEE device /dev/tee0: "
    if [ -c /dev/tee0 ]; then
        echo -e "${GREEN}PRESENT${NC}"
        ((pass++))
    else
        echo -e "${RED}MISSING${NC}"
        ((fail++))
    fi

    # Check tee-supplicant
    echo -n "  ✓ tee-supplicant running: "
    if pgrep -f "tee-supplicant" > /dev/null; then
        echo -e "${GREEN}YES${NC}"
        ((pass++))
    else
        echo -e "${RED}NO${NC}"
        ((fail++))
    fi

    # Check libteec
    echo -n "  ✓ libteec installed: "
    if pkg-config teec 2>/dev/null || ldconfig -p | grep -q libteec; then
        echo -e "${GREEN}YES${NC}"
        ((pass++))
    else
        echo -e "${RED}NO${NC}"
        ((fail++))
    fi

    # Check mission files
    echo -n "  ✓ Mission verification files: "
    local files=(
        "Tools/mission_sign.py"
        "Tools/mission_privkey.pem"
        "src/lib/optee_mission_client/mission_client.h"
        "optee_ta/mission_ta/ta_mission_defines.h"
    )
    local all_exist=1
    for f in "${files[@]}"; do
        if [ ! -f "$f" ]; then
            all_exist=0
            break
        fi
    done
    if [ $all_exist -eq 1 ]; then
        echo -e "${GREEN}ALL PRESENT${NC}"
        ((pass++))
    else
        echo -e "${RED}SOME MISSING${NC}"
        ((fail++))
    fi

    echo ""
    echo -e "  Summary: ${GREEN}${pass} passed${NC}, ${RED}${fail} failed${NC}"
    echo ""

    return $fail
}

# ============================================================================
# PHASE 2: Test Mission Signing
# ============================================================================

phase_mission_signing() {
    echo -e "${YELLOW}[PHASE 2] Mission Signing (Ground Side)${NC}"
    echo ""

    echo "  Testing mission_sign.py..."

    # Generate test signature
    if [ ! -f mission.sha256 ]; then
        python3 Tools/mission_sign.py Tools/test_mission.plan
    fi

    if [ -f mission.sha256 ] && [ -f mission.sig ]; then
        echo -e "  ${GREEN}✓ mission.sha256 generated${NC}"
        echo -e "  ${GREEN}✓ mission.sig generated${NC}"

        # Display sizes
        local hash_size=$(wc -c < mission.sha256)
        local sig_size=$(wc -c < mission.sig)
        echo "    - Hash file: $hash_size bytes"
        echo "    - Sig file: $sig_size bytes"

        echo ""
        return 0
    else
        echo -e "  ${RED}✗ Failed to generate mission files${NC}"
        echo ""
        return 1
    fi
}

# ============================================================================
# PHASE 3: Test Client Library Compilation
# ============================================================================

phase_client_lib() {
    echo -e "${YELLOW}[PHASE 3] Client Library Compilation${NC}"
    echo ""

    echo "  Checking if optee_mission_client can link with PX4..."

    # Try to extract compile flags
    if grep -q "optee_mission_client" src/modules/commander/HealthAndArmingChecks/CMakeLists.txt; then
        echo -e "  ${GREEN}✓ Client lib properly referenced in CMakeLists.txt${NC}"
    else
        echo -e "  ${RED}✗ Client lib NOT in CMakeLists.txt${NC}"
        return 1
    fi

    # Check if TEEC headers are available
    if pkg-config teec 2>/dev/null; then
        echo -e "  ${GREEN}✓ TEEC pkg-config available${NC}"
        pkg-config --cflags --libs teec 2>/dev/null | head -1
    fi

    echo ""
    echo "  Next: Run full PX4 build to verify linking"
    echo "    make px4_sitl 2>&1 | grep -E 'optee|teec|TEEC'"
    echo ""

    return 0
}

# ============================================================================
# PHASE 4: Test TA Compilation (requires SDK)
# ============================================================================

phase_ta_compilation() {
    echo -e "${YELLOW}[PHASE 4] OP-TEE TA Compilation${NC}"
    echo ""

    if ! command -v optee-build &> /dev/null; then
        echo -e "  ${RED}✗ OP-TEE SDK not installed${NC}"
        echo ""
        echo "  To compile the TA, you need the OP-TEE SDK:"
        echo ""
        echo "  Option 1: Install via package manager (if available)"
        echo "    apt-get install optee-os optee-client-dev"
        echo ""
        echo "  Option 2: Build from source"
        echo "    git clone https://github.com/OP-TEE/optee_os.git"
        echo "    cd optee_os"
        echo "    make CFG_OPTEE_CORE_LOG_LEVEL=4  # For debugging"
        echo ""
        echo "  Option 3: Use container/Docker"
        echo "    docker pull optee/optee_os:latest"
        echo ""
        return 0
    fi

    echo -e "  ${GREEN}✓ OP-TEE SDK found${NC}"
    echo ""
    echo "  Compiling TA..."

    cd optee_ta/mission_ta || return 1

    if make 2>&1 | grep -q "8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta"; then
        echo -e "  ${GREEN}✓ TA compiled successfully${NC}"
        echo "    Location: $(pwd)/8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta"
        echo ""
        echo "  Installing TA..."

        if sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/; then
            echo -e "  ${GREEN}✓ TA installed to /lib/optee_armtz/${NC}"
        else
            echo -e "  ${YELLOW}⚠ TA installation requires sudo${NC}"
            echo "    sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/"
        fi
    else
        echo -e "  ${RED}✗ TA compilation failed${NC}"
    fi

    cd - > /dev/null
    echo ""
    return 0
}

# ============================================================================
# PHASE 5: Integration Test
# ============================================================================

phase_integration_test() {
    echo -e "${YELLOW}[PHASE 5] Integration Test${NC}"
    echo ""

    echo "  This phase tests the full PX4 + TA integration:"
    echo ""

    echo "  Step 1: Build PX4 with TA support"
    echo "    cd /home/jetson/PX4-Autopilot"
    echo "    make px4_sitl gz_x500"
    echo ""

    echo "  Step 2: Verify client library compiles"
    echo "    grep 'optee_mission_client' src/modules/commander/.../CMakeLists.txt"
    echo ""

    echo "  Step 3: Start SITL"
    echo "    make px4_sitl gz_x500"
    echo ""

    echo "  Step 4: In another terminal, upload mission"
    echo "    python3 Tools/mission_sign.py Tools/test_mission.plan"
    echo "    python3 Tools/test_hash.py"
    echo ""

    echo "  Step 5: Monitor console for:"
    echo "    - mission_client: TA initialized OK"
    echo "    - mission_client: uploading X items"
    echo "    - Mission hash OK (from TA or normal world)"
    echo "    - Mission signature verified OK (if TA signature check is implemented)"
    echo ""

    return 0
}

# ============================================================================
# PHASE 6: Security Validation
# ============================================================================

phase_security_test() {
    echo -e "${YELLOW}[PHASE 6] Security Validation${NC}"
    echo ""

    echo "  1. Verify waypoints in secure memory (NOT in normal world):"
    echo "     - Check: cat /proc/\$pid/maps (navigator process)"
    echo "       Should NOT show mission data"
    echo ""

    echo "  2. Verify TA rejects invalid signatures:"
    echo "     - Modify mission.sig to corrupt the signature"
    echo "     - Re-upload mission"
    echo "     - Should see: 'Arm blocked: Mission signature invalid'"
    echo ""

    echo "  3. Verify one waypoint per request:"
    echo "     - Modify mission_client_get_waypoint() to print request count"
    echo "     - Fly the drone"
    echo "     - Each waypoint should be a separate RPC call"
    echo ""

    echo "  4. Verify audit logging (once implemented):"
    echo "     - Enable TA logging (IMSG/EMSG in ta_mission.c)"
    echo "     - dmesg | grep -i mission"
    echo ""

    return 0
}

# ============================================================================
# Main Test Runner
# ============================================================================

main() {
    local failed=0

    phase_env_check || ((failed++))
    phase_mission_signing || ((failed++))
    phase_client_lib || ((failed++))
    phase_ta_compilation
    phase_integration_test
    phase_security_test

    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"

    if [ $failed -eq 0 ]; then
        echo -e "${BLUE}║${NC} ${GREEN}✓ All prerequisite tests passed${NC}"
        echo -e "${BLUE}║${NC}"
        echo -e "${BLUE}║${NC}  Next: Compile TA and run integration tests"
    else
        echo -e "${BLUE}║${NC} ${RED}✗ Some tests failed (see above)${NC}"
        echo -e "${BLUE}║${NC}"
        echo -e "${BLUE}║${NC}  Fix the issues and re-run"
    fi

    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

    return $failed
}

main "$@"
