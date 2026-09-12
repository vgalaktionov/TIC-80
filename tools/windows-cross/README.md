# Cross-compile TIC-80 PRO for Windows with Docker

Build a Windows **x86-64** executable from macOS or Linux without installing a
Windows compiler on the host. The recipe enables **PRO and all 12 runtimes**,
including the editors, SDL2 and GPU/CRT rendering. SDL2, the scripting runtimes
and MinGW support libraries are linked statically.

## Run

Install Git and Docker, start Docker with Linux containers enabled, then run
from this repository:

```sh
git submodule update --init --recursive
bash tools/windows-cross/build.sh
```

The script builds the current **committed HEAD**, with its pinned submodule
commits. Uncommitted source edits and existing build output are excluded. It
does not fetch or change branches; update your branch first if needed.

The result is `out/windows-cross/<short-commit>/tic80-pro.exe`. The same directory
contains the source commit, pinned submodule IDs, CMake configuration, build
log, SHA-256 checksum, symbols and verification results. Rebuilding the same
commit replaces that commit's output files.

The source snapshot is mounted read-only and copied into a disposable Debian
container. Compilers, Ruby, Rake and other build dependencies are installed
inside the container. The container and temporary source snapshot are removed
when the build finishes. Package downloads require internet access on each run.
Debian's rolling package updates mean this is not a byte-for-byte reproducible
toolchain. The build uses eight parallel jobs.

## WSL2

Use the same command from a WSL2 Linux terminal. With Docker Desktop for Windows,
enable the WSL2 engine and integration for your distribution under
**Settings → Resources → WSL Integration**; see
[Docker's WSL2 instructions](https://docs.docker.com/desktop/features/wsl/).
Check that `docker version` shows both client and server before building.
Keep the checkout in the Linux filesystem, such as `~/src/TIC-80`.

The recipe was tested on an Apple Silicon Mac using Docker's Linux ARM64
container and MinGW-w64 targeting Windows x86-64. WSL2 and running the resulting
executable on Windows have **not** been tested here. This is not a Windows ARM64
build, and the Linux-only optimized CRT/low-latency options are not enabled.

## Configuration and verification

`container.sh` configures CMake with:

```text
CMAKE_BUILD_TYPE=Release
BUILD_PRO=ON
BUILD_WITH_ALL=ON
BUILD_STATIC=ON
BUILD_SDLGPU=ON
```

All runtime switches are checked after building: Lua, MoonScript, Fennel,
YueScript, JavaScript (QuickJS), Ruby (mruby), Python (pocketpy), Scheme (s7),
Squirrel, Wren, Janet and WebAssembly (wasm3). `verify-runtimes.py` decodes the
Windows executable's registration table to verify all 12 entries are present.
The verification log also reports the executable format and imported DLLs.
The tested build imported only Windows system DLLs. These are build checks,
not Windows runtime tests.

Two details are required for the complete cross-build:

- Janet's CMake rule expects Git Bash and `mingw32-make`. Inside this Linux
  container, `GITBASH=/bin/bash` and a `mingw32-make` alias to native `make`
  let it build its native bootstrap generator. mruby similarly uses native
  Ruby/Rake/GCC for its host compiler before cross-compiling its library.
- The final C++ link explicitly includes `winpthread` for the threading and
  clock symbols needed by the all-runtime build. Parallel LTO speeds up linking.

No source patches or disabled runtimes are required. The archived source omits
Git metadata, so the embedded version uses the archive's default revision;
`commit.txt` records the exact source commit used.
