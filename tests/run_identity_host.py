"""Run the reference identity test with a private, disposable database."""

from pathlib import Path
import subprocess
import sys
import tempfile


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        database = Path(directory).resolve() / "accounts.db"
        subprocess.run([sys.argv[1], str(database)], check=True)
