"""Restore appliance input preferences after XInput device hotplug."""
import os
import select
import subprocess


def run(*args):
    try:
        result = subprocess.run(args, capture_output=True, text=True,
                                env=dict(os.environ, LC_ALL="C"), timeout=5)
    except (subprocess.TimeoutExpired, OSError):
        return ""
    return result.stdout if result.returncode == 0 else ""


def flat_profile(properties):
    for line in properties.splitlines():
        if line.strip().startswith("libinput Accel Profile Enabled ("):
            count = len(line.partition(":")[2].split(","))
            if count in (2, 3):
                return ["0", "1"] + ["0"] * (count - 2)
    return None


def apply():
    run("xset", "r", "rate", "250", "30")
    for device in run("xinput", "list", "--id-only").split():
        if not device.isdecimal():
            continue
        profile = flat_profile(run("xinput", "list-props", device))
        if profile:
            run("xinput", "set-prop", device, "libinput Accel Profile Enabled", *profile)
            run("xinput", "set-prop", device, "libinput Accel Speed", "0")
    print("Input preferences applied: repeat 250ms/30Hz, flat pointers", flush=True)


def main():
    from Xlib import display
    from Xlib.ext import xinput

    connection = display.Display()
    connection.xinput_query_version()
    connection.screen().root.xinput_select_events([(xinput.AllDevices, xinput.HierarchyChangedMask)])
    connection.sync()
    apply()
    while True:
        connection.next_event()
        # A composite receiver adds several devices. Apply after the burst settles.
        while True:
            while connection.pending_events():
                connection.next_event()
            if not select.select([connection], [], [], 0.15)[0]:
                break
        apply()


if __name__ == "__main__":
    main()
