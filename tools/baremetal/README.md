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
