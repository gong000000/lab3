#!/usr/bin/env python3
"""Print docs/REPORT.html to docs/REPORT.pdf with headless Chrome/Edge.

Chrome/Edge honor the CSS @page rules and page breaks, so this produces
the clean, selectable-text PDF edition of the report.  Hancom Office is
only used for the editable HWP edition (see convert_hwp.py).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent
HTML = DOCS / "REPORT.html"
PDF = DOCS / "REPORT.pdf"

CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def find_browser() -> str:
    for path in CANDIDATES:
        if os.path.exists(path):
            return path
    for name in ("chrome", "chrome.exe", "msedge", "msedge.exe"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("Chrome or Edge was not found")


def main() -> None:
    if not HTML.exists():
        sys.exit(f"missing {HTML}; run make_report.py first")

    browser = find_browser()
    url = HTML.resolve().as_uri()
    cmd = [
        browser,
        "--headless=new",
        "--disable-gpu",
        "--no-pdf-header-footer",
        f"--print-to-pdf={PDF}",
        url,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if not PDF.exists() or PDF.stat().st_size == 0:
        sys.exit(f"PDF generation failed:\n{result.stdout}\n{result.stderr}")
    print(f"wrote {PDF} ({PDF.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
