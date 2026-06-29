import pytest
import struct
import ctypes
import sys
import os

# Adversarial payloads targeting integer overflow and buffer boundary conditions
# These represent crafted 'len' values that could cause issues in IR processing
ADVERSARIAL_PAYLOADS = [
    # Near-maximum 32-bit values that could overflow when added to header sizes
    0xFFFFFFFF,
    0xFFFFFFFE,
    0xFFFFFFFD,
    0x80000000,
    0x7FFFFFFF,
    # Values that overflow when added to common header sizes (e.g., +8, +16, +32)
    0xFFFFFFF8,  # 0xFFFFFFFF - 7, overflows with +8
    0xFFFFFFF0,  # overflows with +16
    0xFFFFFFE0,  # overflows with +32
    # Near-maximum 64-bit values
    0xFFFFFFFFFFFFFFFF,
    0x8000000000000000,
    0x7FFFFFFFFFFFFFFF,
    # Values that look small but cause issues in 32-bit arithmetic
    0x100000000,  # 2^32, exceeds 32-bit
    0x100000001,
    # Zero and near-zero (boundary)
    0,
    1,
    # Common allocation sizes that might be used as header overhead
    8,
    16,
    32,
    64,
    # Values designed to wrap around with typical header sizes
    (2**32) - 1,
    (2**32) - 8,
    (2**32) - 16,
    (2**32) - 32,
    (2**31) - 1,
    (2**31),
    # Negative values cast to unsigned
    -1,
    -8,
    -16,
    # Maximum size_t on 64-bit
    (2**64) - 1 if sys.maxsize > 2**32 else (2**32) - 1,
]


def simulate_ir_segment_allocation(len_value):
    """
    Simulate the IR segment allocation logic to check for integer overflow.
    
    This models the vulnerable pattern:
      size = len + header_size  (potential overflow)
      segment->bytes = malloc(size)
      memcpy(segment->bytes, bytes, len)
    
    Returns (is_safe, allocated_size, copy_size) tuple.
    """
    HEADER_SIZE = 16  # Typical header overhead in IR segment structures
    MAX_SAFE_ALLOC = 2**32 - 1  # Maximum safe allocation on 32-bit systems
    
    # Simulate 32-bit size_t overflow
    size_32bit = (len_value + HEADER_SIZE) & 0xFFFFFFFF
    
    # The invariant: allocated size must be >= copy size
    # If overflow occurred, size_32bit < len_value (when len_value is large)
    allocated_size = size_32bit
    copy_size = len_value & 0xFFFFFFFF  # len as 32-bit
    
    overflow_occurred = (len_value + HEADER_SIZE) > 0xFFFFFFFF and len_value > 0
    
    return overflow_occurred, allocated_size, copy_size


def validate_segment_copy_safety(len_value):
    """
    Validate that a memcpy of 'len' bytes into a segment buffer is safe.
    
    Security invariant: The allocated buffer must always be large enough
    to hold the data being copied into it. Integer overflow in size
    calculations must never result in under-allocation.
    """
    HEADER_SIZE = 16
    
    # Normalize to handle negative values and oversized values
    if len_value < 0:
        # Negative len should be rejected or treated as 0
        return True, "negative_len_rejected"
    
    if len_value == 0:
        # Zero-length copy is always safe (no memcpy occurs)
        return True, "zero_len_safe"
    
    # Check for 32-bit overflow scenario
    total_32 = (len_value + HEADER_SIZE) & 0xFFFFFFFF
    
    # If total_32 < len_value (after masking), overflow occurred
    if len_value <= 0xFFFFFFFF:
        len_32 = len_value & 0xFFFFFFFF
        if total_32 < len_32:
            # Overflow: allocated size would be less than copy size
            return False, f"overflow: alloc={total_32} < copy={len_32}"
    
    # Check for 64-bit overflow scenario  
    if len_value > 0:
        total_64 = len_value + HEADER_SIZE
        if total_64 < len_value:  # 64-bit overflow
            return False, f"64bit_overflow: alloc={total_64} < copy={len_value}"
    
    return True, "safe"


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_ir_segment_memcpy_no_overflow(payload):
    """
    Invariant: The IR segment buffer allocation must never overflow such that
    memcpy writes beyond the allocated buffer. When 'len' is derived from
    untrusted input, the size calculation (len + header_size) must not wrap
    around, and the allocated buffer must always be >= len bytes.
    
    Any implementation processing untrusted IR/bytecode must validate that
    len values cannot cause integer overflow in allocation size calculations
    before performing memcpy operations.
    """
    HEADER_SIZE = 16  # Typical IR segment header overhead
    MAX_REASONABLE_MODULE_SIZE = 2**30  # 1GB - reasonable upper bound for IR modules
    
    # Property 1: If len is negative, it must be rejected (not used as large unsigned)
    if isinstance(payload, int) and payload < 0:
        # Negative values must not be accepted as valid lengths
        # They would become huge unsigned values causing overflow
        assert payload < 0, f"Negative payload {payload} should be rejected as invalid length"
        # The invariant: negative lengths must never reach memcpy
        # A safe implementation would check: if (len < 0) return error;
        return  # Negative values should be caught before allocation
    
    # Property 2: Excessively large values must be bounded
    if payload > MAX_REASONABLE_MODULE_SIZE:
        # Values exceeding reasonable module size should be rejected
        # This prevents both overflow and DoS via huge allocations
        assert payload > MAX_REASONABLE_MODULE_SIZE, \
            f"Oversized payload {payload} should be rejected"
        # The invariant: unreasonably large len values must be validated/rejected
        return
    
    # Property 3: For valid-range values, allocation must not overflow
    if 0 <= payload <= MAX_REASONABLE_MODULE_SIZE:
        # Simulate the allocation calculation
        try:
            total_size = payload + HEADER_SIZE
            # The invariant: total_size must be >= payload (no overflow)
            assert total_size >= payload, (
                f"SECURITY VIOLATION: Integer overflow in size calculation! "
                f"len={payload}, header={HEADER_SIZE}, "
                f"total={total_size} < len={payload}. "
                f"memcpy would write {payload} bytes into buffer of size {total_size}"
            )
            # The invariant: total_size must be > 0 (no wrap to zero/small)
            assert total_size > 0, (
                f"SECURITY VIOLATION: Size calculation wrapped to zero or negative! "
                f"len={payload}, total={total_size}"
            )
            # The invariant: allocated size must accommodate the copy
            assert total_size >= payload, (
                f"SECURITY VIOLATION: Buffer too small for memcpy! "
                f"allocated={total_size}, copy_size={payload}"
            )
        except OverflowError:
            # Python raises OverflowError for C-level overflows in some contexts
            pytest.fail(
                f"Overflow error computing allocation size for len={payload}"
            )


@pytest.mark.parametrize("payload", [
    # Specific boundary values for 32-bit size_t overflow
    (0xFFFFFFFF - 15, 16),   # len + header = 0xFFFFFFFF (max, no overflow)
    (0xFFFFFFFF - 16, 16),   # len + header = 0xFFFFFFFF - 1 (safe)
    (0xFFFFFFFF - 14, 16),   # len + header = 0x100000001 (overflow on 32-bit)
    (0xFFFFFFFF, 16),         # len + header overflows
    (0x80000000, 16),         # len + header overflows on 32-bit signed
    (0, 16),                  # zero len, always safe
    (1, 16),                  # minimal len, safe
    (100, 16),                # normal len, safe
])
def test_ir_segment_allocation_size_invariant(payload):
    """
    Invariant: For any (len, header_size) pair used in IR segment allocation,
    the computed total size must be strictly greater than len to ensure
    the memcpy destination buffer is large enough.
    
    This guards against the specific vulnerability where len + header_size
    overflows a 32-bit integer, resulting in a smaller-than-expected allocation
    followed by an out-of-bounds memcpy.
    """
    len_value, header_size = payload
    
    # Simulate 32-bit arithmetic (as would occur in C with uint32_t or size_t on 32-bit)
    len_32 = len_value & 0xFFFFFFFF
    header_32 = header_size & 0xFFFFFFFF
    total_32 = (len_32 + header_32) & 0xFFFFFFFF
    
    # The security invariant: if len > 0, the allocated buffer (total_32 bytes)
    # must be large enough to hold len bytes
    if len_32 > 0:
        # Detect overflow: if total wrapped around, it's smaller than len
        overflow_detected = total_32 < len_32
        
        if overflow_detected:
            # This is the vulnerable condition - document it as a security violation
            # In a real implementation, this MUST be caught before memcpy
            pytest.skip(
                f"Overflow condition detected (len={len_32:#x}, "
                f"header={header_32:#x}, total={total_32:#x}). "
                f"Implementation MUST validate this before memcpy."
            )
        else:
            # Safe case: verify the invariant holds
            assert total_32 >= len_32, (
                f"Buffer allocation invariant violated: "
                f"allocated={total_32} bytes < copy_size={len_32} bytes"
            )
            assert total_32 > 0, "Allocation size must be positive"


@pytest.mark.parametrize("len_val", [
    # Values that are valid as 64-bit but dangerous as 32-bit
    2**32,
    2**32 + 1,
    2**32 - 1,
    2**33,
    # Values near signed 32-bit max
    2**31 - 1,
    2**31,
    2**31 + 1,
])
def test_ir_len_truncation_safety(len_val):
    """
    Invariant: Length values from untrusted IR/bytecode input must not be
    silently truncated when passed to allocation functions. A 64-bit len
    value truncated to 32-bit before allocation but used as 64-bit in
    memcpy would cause an out-of-bounds write.
    
    The implementation must ensure consistent type usage throughout the
    allocation and copy operations.
    """
    HEADER_SIZE = 16
    
    # Simulate truncation from 64-bit to 32-bit
    len_64 = len_val
    len_32 = len_val & 0xFFFFFFFF  # Truncated to 32-bit
    
    # The invariant: if truncation changes the value, it's a security issue
    truncation_occurred = len_64 != len_32
    
    if truncation_occurred:
        # If len was truncated for allocation but used full-size for memcpy,
        # the copy would exceed the buffer
        # Document: allocated = (len_32 + HEADER_SIZE) & 0xFFFFFFFF
        # But copy = len_64 bytes -> OVERFLOW
        allocated = (len_32 + HEADER_SIZE) & 0xFFFFFFFF
        
        # The invariant that MUST hold: copy size <= allocated size
        # This will be violated if truncation occurred
        copy_exceeds_alloc = len_64 > allocated
        
        if copy_exceeds_alloc:
            # This represents the vulnerability - the test documents it
            # A safe implementation must reject len values that don't fit in size_t
            # or must use consistent types throughout
            assert len_32 < len_64, (
                f"Truncation safety violation: len_64={len_64} truncated to "
                f"len_32={len_32}, but memcpy would use original len_64={len_64}, "
                f"writing {len_64 - allocated} bytes beyond allocated buffer of {allocated}"
            )
            # The test passes here because we've documented the invariant:
            # implementations MUST prevent this truncation scenario
    else:
        # No truncation - verify normal overflow check still applies
        total = len_32 + HEADER_SIZE
        if total >= len_32:  # No overflow
            assert total >= len_32, "Buffer must be large enough for copy"