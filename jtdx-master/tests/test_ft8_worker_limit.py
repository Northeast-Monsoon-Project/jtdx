#!/usr/bin/env python3
"""Real FT8 nine-pass decoding with fewer OpenMP workers than requested."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("share", type=Path)
    parser.add_argument("--wine", action="store_true")
    args = parser.parse_args()
    fixture, share = args.fixture.resolve(), args.share.resolve()
    for limit in (1, 2, 3):
        for modern in (0, 1):
            with tempfile.TemporaryDirectory(prefix="jtdxf-ft8-workers-") as work:
                command = [str(fixture), "8", str(modern), "3702026", "-10", "crowded", "8",
                           str(share), "mixed", "3"]
                if args.wine:
                    command = ["wine"] + command
                env = dict(os.environ, OMP_THREAD_LIMIT=str(limit), OMP_DYNAMIC="FALSE")
                result = subprocess.run(command, cwd=work, env=env, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
                if result.returncode or "@@TIMING" not in result.stdout:
                    raise AssertionError(result.stdout)
                truth = {line[len("@@TRUTH "):].strip() for line in result.stdout.splitlines()
                         if line.startswith("@@TRUTH ")}
                lines = [line for line in result.stdout.splitlines()
                         if line[:6].isdigit() and "~" in line]
                missed = [message for message in truth if not any(message in line for line in lines)]
                if missed:
                    raise AssertionError(f"workers={limit}, modern={modern}, missed={missed}\n{result.stdout}")
                print(f"PASS actual workers <= {limit}, requested 8, modern={modern}, 9 passes")


if __name__ == "__main__":
    main()
