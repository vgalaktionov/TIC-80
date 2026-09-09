# Bare-metal validation tools

These host-side checks cover the code that is difficult to exercise without a
Raspberry Pi 4. Run the memory-safety tests with Clang:

```sh
tmp=$(mktemp -d)
clang++ -std=c++14 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/baremetal/logring_test.cpp -o "$tmp/logring" && "$tmp/logring"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/baremetal/v3d_buffer_test.c -o "$tmp/v3d-buffer" && "$tmp/v3d-buffer"
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare \
  -fsanitize=address,undefined tools/baremetal/v3d_texture_layout_test.c \
  -o "$tmp/v3d-texture" && "$tmp/v3d-texture"
```

`v3d_packet_layout_test.c` emits a representative packet corpus. Compile it as
an executable on a 64-bit host and as an object with the bare-metal AArch32
compiler, extract the `.v3d_probe` section from the latter, and compare the two
166-byte outputs. This catches compiler-ABI changes in the packed V3D records.

`crt_shader_assembler.c` reproduces and validates the embedded V3D 4.2 fragment
shader. It requires `v3dAssembler.h` from v3d-toolkit commit
`a3e9d4a92022d22408ab6b8e9f87709aa08d8c96` on the include path:

```sh
cc -std=c11 -I/path/to/v3d-toolkit/src tools/baremetal/crt_shader_assembler.c \
  -o /tmp/crt-shader && /tmp/crt-shader
```
## HDMI Recovery Regression Test

Run the production recovery state machine with simulated firmware and time:

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tools/baremetal/hdmi_recovery_test.cpp -o /tmp/tic80-hdmi-test
/tmp/tic80-hdmi-test
```

The LAN logger accepts `POST /hdmi/status` and `POST /hdmi/reset`, with
`X-TIC80-Debug: 1`. Both return 202 and queue work for the render task; results
appear in `/log`. These are unauthenticated trusted-LAN diagnostics, not services
to expose to the internet. The custom header blocks ordinary cross-origin browser
forms, but is not authentication.

```sh
curl -X POST -H 'X-TIC80-Debug: 1' http://192.168.2.28:8080/hdmi/status
curl -X POST -H 'X-TIC80-Debug: 1' http://192.168.2.28:8080/hdmi/reset
```

Keyboard or mouse attachment schedules one debounced HDMI link reset, including
initial attachment. This also briefly blanks HDMI for a directly plugged-in input
device: USB attachment is a fallback heuristic, not proof of a KVM switch.
Firmware activation does not prove that a physical monitor is displaying pixels.

## GPU Reset Regression Test

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tools/baremetal/v3d_power_test.cpp -o /tmp/tic80-v3d-power-test
/tmp/tic80-v3d-power-test
```

This tests the production reset sequence with simulated ASB acknowledgements,
including failures on each bridge, rollback, and clock wraparound. It cannot
validate GPU command execution. The runtime only logs `first GPU frame completed`
after both binning and rendering counters advance.

The reset follows the BCM2711 ASB/reset ordering in
[bcm2835-power.c](https://github.com/raspberrypi/linux/blob/a1073743767f9e7fdc7017ababd2a07ea0c97c1c/drivers/pmdomain/bcm/bcm2835-power.c).
Submission uses the queued registers as in
[Linux v3d_sched.c](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/v3d/v3d_sched.c),
without legacy per-thread halt/resume writes. Clock management remains with the
firmware; this is not a full Linux power-domain implementation.

## Render List Regression Test

This compiles the production command builders without hardware access. Use GNU
C++ because the vendored texture header uses GNU pointer arithmetic:

```sh
g++ -std=c++14 -Iinclude -Wno-pointer-arith -Wno-narrowing \
  -fsanitize=address,undefined tools/baremetal/v3d_render_list_test.cpp \
  -o /tmp/tic80-v3d-render-list-test
/tmp/tic80-v3d-render-list-test
```

It reproduces the old depth-store layout exceeding its allocation, checks that
the fullscreen pass emits only a raster color store with depth disabled, and
checks packet fields, padded framebuffer stride, and command-buffer exhaustion.
It does not execute shaders or validate physical GPU behavior.
