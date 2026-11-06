"""Pack banked FRLG PC graphics without changing the source palette indices."""
from pathlib import Path
import sys
source, destination = map(Path, sys.argv[1:])
pixels = source.read_bytes()
if len(pixels) % 64:
    raise ValueError("Expected whole 8x8 tiles")
destination.write_bytes(bytes((pixels[i] & 15) | ((pixels[i+1] & 15) << 4)
    for i in range(0, len(pixels), 2)))
