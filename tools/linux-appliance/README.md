# TIC-80 Linux Appliance (Prototype)

Separate from the Circle build. Uses Raspberry Pi OS Lite ARM64 (Trixie), the
distribution Linux/Mesa stack, Xorg/Openbox without a desktop shell, PipeWire,
and the upstream Release/PRO SDL-GPU frontend. No hand-written GPU driver.
The application is built from the checked-out Git commit (`HEAD`), not uncommitted
application edits. The initial Linux branch carries only the empty-SURF escape
and Options menu index fixes on top of upstream. The bare-metal CRT,
HDMI recovery, networking, HTTP logger and code-sync rewrite are not included.

## Build

Run `tools/linux-appliance/build.sh` from the repository on an ARM64 Docker
engine (including Apple Silicon Docker Desktop). At least 40 GiB of free host
space is required, plus sufficient free space inside Docker's VM. Allow substantial
download/build time. pi-gen is pinned in the Dockerfile; apt packages follow
the distribution repositories, so this is not a bit-reproducible build.

To preconfigure Wi-Fi, pass `--wifi-ssid 'your SSID' --wifi-password 'your password'`
to the build script, or `--wifi-from-wpa /Volumes/BOOTFS/wpa_supplicant.conf`
to import a single network from an existing card. These options are mutually
exclusive. The importer supports plain quoted strings and hexadecimal SSIDs/PSKs;
use explicit arguments for escaped wpa_supplicant values. Explicit arguments may
appear in shell history and process listings. Credentials are not printed or
written into the repository: a mode-600 temporary file is mounted read-only
into the builder and removed on exit. The generated image and Docker work volume
contain the private network profile, so do not publish them. A build without
Wi-Fi arguments removes the previous build's appliance Wi-Fi profile.

The privileged builder uses Docker's Linux VM and a named work volume; it does
not mount or write host disks. Output is in `build/linux-appliance/`.
No commit or push is performed. A failed build is not a usable release.

Local checks: `python3 tools/linux-appliance/test_runtime.py`. The image build
also runs an ARM64 Xvfb smoke test for fullscreen and CRT on/off/on screenshots.
This uses software GL and does not validate the Pi's V3D driver or performance.
Screenshots are exported to `build/linux-appliance/smoke-test/`.

Compilation and ARM64 software-GL smoke tests have passed during development.
The final image must pass them again before export; Pi hardware remains untested.

## Boot

This is a WHOLE-DISK image, not files to copy over the Circle boot partition.
Back up existing cartridges and configuration before flashing. No automatic
migration from Circle is implemented. Do not overwrite the only copy of a game.
Use Raspberry Pi Imager's custom-image option with the generated `.img.xz`.
Keep write verification enabled. Do not apply Imager OS customizations over the
image's preconfigured account and network settings. This erases the entire card.

If Wi-Fi was not supplied at build time, on the flashed boot partition rename `tic80-wifi.nmconnection.example` to
`tic80-wifi.nmconnection` and enter the SSID and WPA password. NetworkManager
imports it on boot. The boot copy contains plaintext credentials; after a
successful connection it can be removed. The root-owned internal copy remains.
Keyfile escaping follows NetworkManager (backslash is `\\`). The default Wi-Fi
regulatory country is NL; change WPA_COUNTRY in the build config if necessary.
Ethernet works without configuration. Network readiness does not block TIC-80.

TIC-80 starts fullscreen at the console, not SURF. F6 enables/disables the
existing desktop CRT shader; options are saved. Cartridges and configuration
live in `/home/tic80/cartridges` on the writable ext4 filesystem. This prototype
does not yet use a read-only root or a separate data partition. Shut down cleanly;
it is not designed to tolerate arbitrary power cuts without data loss. Exit
TIC-80 normally to power off; wait for shutdown before removing power. A crashing
session restarts instead. Openbox only provides fullscreen/focus management;
there are no panels, desktop menus or window decorations.

Logs are available over SSH with `journalctl -b`, or follow TIC-80 output with
`journalctl -b -t tic80-session -f`. The journal retains up to 16 MiB in RAM,
including pre-network boot messages; logs do not survive a reboot.
There is no HTTP log server. SSH is enabled with public-key authentication:
`ssh tic80@tic80.local` (or use the IP address). The builder installs
`~/.ssh/id_ed25519.pub`; override with `SSH_PUBLIC_KEY_FILE=/absolute/path/key.pub`.
Only the public key is mounted into the builder. Password and root SSH login are
disabled. The `tic80` account has passwordless sudo for remote maintenance;
console autologin also grants local administrative access. This is a personal
appliance image, not a locked-down public kiosk.

PipeWire/WirePlumber select audio devices. USB hotplug, MiniFuse channel mapping,
KVM first-connection behavior and Pi GPU performance need actual hardware tests.
`glxinfo -B` is captured in the journal to distinguish V3D from software rendering.
No claim of hardware validation is made by compiling the image.
