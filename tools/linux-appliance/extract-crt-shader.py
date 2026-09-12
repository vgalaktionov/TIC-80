"""Extract the original production shader for the standalone baseline test."""
from pathlib import Path
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
args = parser.parse_args()
source = Path("src/system/sdl/main.c").read_text()
start = source.index("    static const char PixelShader[] =")
end = source.index("    u32 vertex =", start)
args.output.write_text(source[start:end])
