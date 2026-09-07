// Exercise the fixed-capacity V3D command-list allocator at its boundaries.

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define V3D_ARCH_AARCH64 0
#define V3D_IMPLEMENTATION
#include "../../src/system/baremetalpi/v3d/v3d.h"

int main(void)
{
    _Alignas(64) uint8_t storage[128];
    memset(storage, 0, sizeof storage);
    v3d_static_buffer buffer = {storage + 1, 0, 16};

    uint32_t value = 0x12345678;
    v3d_buffer_write(&buffer, &value, sizeof value);
    assert(buffer.used == 4);

    v3d_buffer_align(&buffer, 8);
    assert(((uintptr_t)buffer.start + buffer.used) % 8 == 0);
    assert(buffer.used <= buffer.capacity);

    const int beforeFailedClaim = buffer.used;
    assert(v3d_buffer_claim_memory(&buffer, 32) == 0);
    assert(buffer.used == beforeFailedClaim);

    uint8_t excess[32];
    memset(excess, 0xa5, sizeof excess);
    v3d_buffer_write(&buffer, excess, sizeof excess);
    assert(buffer.used == buffer.capacity);
    assert(v3d_buffer_out_of_memory(&buffer));

    buffer.used = 15;
    v3d_buffer_align(&buffer, 64);
    assert(buffer.used == buffer.capacity);

    buffer.used = 1;
    v3d_buffer_align(&buffer, 3);
    assert(buffer.used == buffer.capacity);

    return 0;
}
