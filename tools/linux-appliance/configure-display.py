"""Set the appliance's HDMI0 KMS mode, preserving other kernel arguments."""
from pathlib import Path
import sys


def configure(text):
    args = [arg for arg in text.split() if not arg.startswith("video=HDMI-A-1:")]
    args.append("video=HDMI-A-1:1920x1080@60D")
    return " ".join(args) + "\n"


if __name__ == "__main__":
    path = Path(sys.argv[1])
    path.write_text(configure(path.read_text()))
