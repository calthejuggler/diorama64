#!/usr/bin/env python3
"""./build.py — builds Diorama64 and installs it on your Apple Vision Pro or launches it in the simulator. It asks what
it needs on the first run (or with --reconfigure) and remembers the answers in local/build.json. ./build.py --help lists
the flags."""
import pathlib, sys

if sys.version_info < (3, 9):
    sys.exit("Diorama64's build needs Python 3.9 or newer (the one Xcode installs as /usr/bin/python3 is fine)")
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "scripts"))
import guided_build  # noqa: E402

sys.exit(guided_build.run_main(sys.argv[1:]))
