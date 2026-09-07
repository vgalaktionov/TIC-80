#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define v3d_memcmp memcmp
#define v3d_vsnprintf vsnprintf
#define V3D_ASSEMBLER_IMPLEMENTATION
#include "v3dAssembler.h"

#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))

static const char* FragmentShader[] = {
    "nop ; nop ; ldvary.r0; wrtmuc",
    "nop ; fmul r1, r0, rf0 ; ldvary.r2; wrtmuc",
    "fadd rf3, r1, r5 ; fmul r3, r2, rf0",
    "fadd rf4, r3, r5 ; nop",
    "nop ; mov tmut, rf4",
    "nop ; nop",
    "nop; mov tmus, rf3",
    "nop ; nop ; ldtmu.r4",
    "nop ; nop ; ldtmu.r0",
    "nop ; fmul rf7, r4.l, 0x3f800000",
    "nop ; fmul rf8, r4.h, 0x3f800000",
    "nop ; fmul rf9, r0.l, 0x3f800000",
    "nop ; fmul rf10, r0.h, 0x3f800000",
    "nop ; nop ; wrtmuc",
    "nop ; nop ; wrtmuc",
    "nop ; mov tmut, rf4 ; thrsw",
    "nop ; nop ; thrsw",
    "nop; mov tmus, rf3",
    "nop ; nop ; ldtmu.r4",
    "nop ; fmul rf7, r4.l, rf7 ; ldtmu.r0",
    "nop; fmul rf8, r4.h, rf8",
    "nop; fmul rf9, r0.l, rf9",
    "nop; fmul rf10, r0.h, rf10",
    "vfpack tlb, rf7, rf8 ; nop ; thrsw",
    "vfpack tlb, rf9, rf10 ; nop",
    "nop ; nop",
};

int main(void)
{
    struct v3d_device_info device = {0};
    struct v3d_qpu_instr unpacked[ARRAY_SIZE(FragmentShader)] = {0};
    v3d_uint64 packed[ARRAY_SIZE(FragmentShader)] = {0};
    device.ver = 42;
    device.has_accumulators = 1;

    for (unsigned i = 0; i < ARRAY_SIZE(FragmentShader); ++i)
    {
        struct v3d_qpu_assemble_arguments args = {0};
        args.devinfo = device;
        args.assembly = FragmentShader[i];
        if (!v3d_qpu_assemble(&args))
        {
            fprintf(stderr, "instruction %u, column %d: %s\n%s\n", i,
                    args.errorAtOffset, args.errorMessage, FragmentShader[i]);
            return 1;
        }
        unpacked[i] = args.instruction;
        if (!v3d_qpu_instr_pack(&device, &unpacked[i], &packed[i]))
        {
            fprintf(stderr, "instruction %u failed to pack\n", i);
            return 1;
        }
    }

    struct v3d_qpu_validate_result result = {0};
    if (!v3d_qpu_validate(&device, unpacked, ARRAY_SIZE(unpacked), &result))
    {
        fprintf(stderr, "validation failed at instruction %d: %s\n",
                result.errorInstructionIndex, result.errorMessage);
        return 1;
    }

    for (unsigned i = 0; i < ARRAY_SIZE(packed); ++i)
        printf("0x%016llxULL,%s", packed[i], (i + 1) % 3 ? " " : "\n");

    return 0;
}
