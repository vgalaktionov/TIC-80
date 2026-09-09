// Test the production command builders without accessing physical registers.
#include <cassert>
#include <cstdint>
#include <cstdio>
static void DataSyncBarrier() {}
static void CleanAndInvalidateDataCacheRange(uintptr_t, unsigned) {}
struct CTimer { static unsigned GetClockTicks() { assert(false); return 0; } };
void tic80SerialDebug(const char*) {}
#define TIC80_V3D_RENDERER_TEST
#include "../../src/system/baremetalpi/v3d_renderer.cpp"

static unsigned bits(const uint8_t* data, unsigned start, unsigned count)
{
    unsigned value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= unsigned((data[(start + i) / 8] >> ((start + i) % 8)) & 1) << i;
    return value;
}

int main()
{
    // Reproduce the old depth layout overwriting the following command arena.
    unsigned maximum = 0, commandHits = 0;
    const unsigned oldSize = 960 * 544 * 2;
    for (unsigned y = 0; y < 544; ++y)
        for (unsigned x = 0; x < 960; ++x)
        {
            unsigned offset = v3d_get_uif_xor_pixel_offset(2, 552, x, y);
            if (offset + 2 > maximum) maximum = offset + 2;
            if (offset >= oldSize && offset < oldSize + 12288) ++commandHits;
        }
    assert(maximum > oldSize && commandHits);
    printf("Old depth layout: %u bytes required, %u allocated, %u command-region writes\n",
           maximum, oldSize, commandHits);

    uint8_t render[4096] = {}, indirect[4096] = {}, binning[4096] = {};
    Gpu.rendering = {render, 0, sizeof render};
    Gpu.indirect = {indirect, 0, sizeof indirect};
    Gpu.binning = {binning, 0, sizeof binning};
    Gpu.tileAllocation = reinterpret_cast<uint8_t*>(0x10000000);
    Gpu.framebuffer = reinterpret_cast<uint32_t*>(0x30000000);
    Gpu.framebufferPitch = 960;
    assert(prepareBinning(reinterpret_cast<v3d_gl_shader_state_record*>(0x20000000), 4));
    assert(prepareRendering());
    assert(binning[25] == 96);
    assert(bits(binning + 26, 12, 3) == 7); // depth ALWAYS
    assert(bits(binning + 26, 15, 4) == 0); // no depth/stencil updates or tests
    assert(bits(render + 1, 46, 1) == 1); // early Z disabled

    // Packet offsets from Mesa's v3d_packet.xml, not C++ field accessors.
    const uint8_t prefix[] = {125, 26, 56, 2, 54, 0, 0, 0, 0, 21, 0};
    assert(!memcmp(indirect, prefix, sizeof prefix));
    assert(indirect[11] == 29);
    const uint8_t* store = indirect + 12;
    assert(bits(store, 0, 4) == 0); // sole output is color target 0
    assert(bits(store, 4, 3) == 0); // raster, no UIF padding
    assert(bits(store, 12, 6) == 27); // RGBA8
    assert(bits(store, 28, 20) == 3840);
    assert(bits(store, 64, 32) == 0x30000000);
    assert(indirect[24] == 25); // clear follows color store, no depth store
    assert(indirect[26] == 27 && indirect[27] == 18);
    assert(Gpu.indirect.used == 28);
    assert(bits(render + 1, 8, 16) == OutputWidth);
    assert(bits(render + 1, 24, 16) == OutputHeight);
    assert(render[Gpu.rendering.used - 1] == 13);
    printf("Production render list: %d bytes, generic list: %d bytes\n",
           Gpu.rendering.used, Gpu.indirect.used);
    // A padded framebuffer pitch stays a byte stride, not a tiled height.
    memset(render, 0, sizeof render); memset(indirect, 0, sizeof indirect);
    Gpu.rendering.used = Gpu.indirect.used = 0;
    Gpu.framebufferPitch = 1024;
    assert(prepareRendering());
    assert(bits(indirect + 12, 28, 20) == 4096);
    assert(Gpu.indirect.used == 28);
    // Allocation exhaustion must not emit a partially usable list.
    Gpu.rendering = {render, 0, 1};
    assert(!prepareRendering());
    Gpu.rendering = {render, 0, sizeof render};
    Gpu.indirect = {indirect, 0, 1};
    assert(!prepareRendering());
}
