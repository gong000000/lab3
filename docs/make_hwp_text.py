#!/usr/bin/env python3
"""Create an editable HWP edition from REPORT.md as plain text.

The formatted edition is REPORT.pdf, rendered from REPORT.html.  Hancom's
HTML/DOCX import is unreliable in this environment, so this fallback opens
Hancom Office, clears the body, inserts the Markdown report as paragraphs,
and saves REPORT.hwp.  The result is editable but not typeset.
"""
from __future__ import annotations

import os
import sys
import tempfile
import time
from pathlib import Path

try:
    import win32com.client
except ImportError:
    sys.exit("pywin32 is required: pip install pywin32")

DOCS = Path(__file__).resolve().parent
REPORT_MD = DOCS / "REPORT.md"
HWP = DOCS / "REPORT.hwp"
TMP = Path(tempfile.gettempdir()) / "REPORT_text.hwp"
CHUNK_LINES = 100


def insert_lines(hwp, lines: list[str]) -> None:
    for start in range(0, len(lines), CHUNK_LINES):
        print(f"insert {start}/{len(lines)}", flush=True)
        chunk = "\r\n".join(lines[start:start + CHUNK_LINES])
        if start + CHUNK_LINES < len(lines):
            chunk += "\r\n"
        pset = hwp.HParameterSet.HInsertText
        hwp.HAction.GetDefault("InsertText", pset.HSet)
        pset.Text = chunk
        hwp.HAction.Execute("InsertText", pset.HSet)


def replace_when_unlocked(source: Path, target: Path) -> None:
    last_error: PermissionError | None = None
    for _ in range(50):
        try:
            os.replace(source, target)
            return
        except PermissionError as exc:
            last_error = exc
            time.sleep(0.2)
    raise last_error if last_error is not None else OSError("replace failed")


def main() -> None:
    if not REPORT_MD.exists():
        sys.exit(f"missing {REPORT_MD}; run make_report.py first")

    lines = REPORT_MD.read_text(encoding="utf-8").splitlines()
    print(f"{len(lines)} lines", flush=True)
    hwp = win32com.client.Dispatch("HWPFrame.HwpObject")
    print("dispatch ok", flush=True)
    try:
        try:
            hwp.XHwpWindows.Item(0).Visible = False
        except Exception:
            pass

        hwp.HAction.Run("FileNew")
        print("new ok", flush=True)
        hwp.HAction.Run("SelectAll")
        hwp.HAction.Run("Delete")
        print("delete ok", flush=True)
        insert_lines(hwp, lines)
        print("insert done", flush=True)
        hwp.SaveAs(str(TMP), "HWP", "")
        pages = hwp.PageCount
        print(f"saved {pages}", flush=True)
    finally:
        try:
            hwp.Quit()
        except Exception:
            pass

    replace_when_unlocked(TMP, HWP)
    print(f"wrote {HWP} ({HWP.stat().st_size} bytes, {pages} pages)")


if __name__ == "__main__":
    main()
