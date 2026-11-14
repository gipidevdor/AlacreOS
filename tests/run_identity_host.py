"""Run the reference identity test in a private, disposable directory."""

import subprocess
import sys
import tempfile


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run([sys.argv[1], directory], check=True)
