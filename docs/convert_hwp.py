#!/usr/bin/env python3
"""Convert docs/REPORT.html to HWP and PDF with Hancom Office.

Hancom Office 2024 in this environment opens HTML reliably (DOCX import
returned False), so the HTML edition is used as the intermediate format.
The HTML importer ignores CSS page-break rules, so after import we scan
the paragraphs through the clipboard and insert real page breaks before
the major section headings.

The generated HWP/PDF are committed to the repository so the report can
be read without Hancom Office.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

try:
    import win32clipboard
    import win32com.client
except ImportError:
    sys.exit("pywin32 is required: pip install pywin32")

DOCS = Path(__file__).resolve().parent
HTML = DOCS / "REPORT.html"
DOCX = DOCS / "REPORT.docx"
HWP = DOCS / "REPORT.hwp"
PDF = DOCS / "REPORT_hwp.pdf"

SECTION_HEADINGS = [
    "1. 프로젝트 개요",
    "1.4 개발 환경",
    "2. 설계 구성 요소",
    "2.2 시스템 구조",
    "3. 설계 제한 요소",
    "4. 설계 내용",
    "4.5 내장 명령 구현",
    "4.8 원격 쉘 확장",
    "5.2 프로젝트 완성도 평가",
    "5.3 일정 계획 평가",
    "5.4 역할 수행 평가",
    "5.5 위험 처리",
    "6. 소감",
    "부록 A. 빌드 및 실행",
    "부록 B. 테스트 항목 요약",
    "부록 C. 소스 규모",
]


def normalize(text: str) -> str:
    return "".join(text.split())


def convert(hwp, source: Path, fmt: str) -> None:
    result = hwp.Open(str(source), fmt, "forceopen:true")
    print(f"open {source.name} ({fmt}): {result}, pages={hwp.PageCount}")
    if not result:
        raise RuntimeError(f"Hancom Office could not open {source}")


def clipboard_text() -> str:
    last_error = None
    for _ in range(10):
        try:
            win32clipboard.OpenClipboard()
            try:
                if win32clipboard.IsClipboardFormatAvailable(
                        win32clipboard.CF_UNICODETEXT):
                    return win32clipboard.GetClipboardData(
                        win32clipboard.CF_UNICODETEXT)
            finally:
                win32clipboard.CloseClipboard()
        except Exception as exc:  # clipboard can be briefly locked
            last_error = exc
        time.sleep(0.05)
    raise RuntimeError(f"could not read clipboard: {last_error}")


def paragraph_text(hwp, index: int) -> str:
    hwp.SetPos(0, index, 0)
    try:
        hwp.SelectText(index, 0, index + 1, 0)
    except Exception:
        hwp.SelectText(index, 0, index, 16)
    hwp.Run("Copy")
    time.sleep(0.01)
    return clipboard_text().replace("\r\n", " ").replace("\n", " ").strip()


def insert_section_page_breaks(hwp) -> None:
    """Find section headings and put a page break before each of them."""
    max_para = 0
    for i in range(0, 3000):
        hwp.SetPos(0, i, 0)
        pos = hwp.GetPos()
        if pos[1] != i:
            break
        max_para = i
    print(f"paragraphs: {max_para + 1}")

    found: dict[str, int] = {}
    for i in range(max_para + 1):
        try:
            text = paragraph_text(hwp, i)
        except Exception:
            continue
        normalized = normalize(text)
        for heading in SECTION_HEADINGS:
            if heading not in found and normalized.startswith(normalize(heading)):
                found[heading] = i

    missing = [h for h in SECTION_HEADINGS if h not in found]
    if missing:
        print(f"warning: headings not found: {missing}", file=sys.stderr)

    before = hwp.PageCount
    for heading, index in sorted(found.items(), key=lambda item: item[1],
                                 reverse=True):
        hwp.SetPos(0, index, 0)
        hwp.Run("BreakPage")
        print(f"page break before {heading!r} (paragraph {index})")
    print(f"pages: {before} -> {hwp.PageCount}")


def main() -> None:
    if not HTML.exists():
        sys.exit(f"missing {HTML}; run make_report.py first")

    hwp = win32com.client.Dispatch("HWPFrame.HwpObject")
    try:
        try:
            hwp.RegisterModule("FilePathCheckDLL", "FilePathCheckerModule")
        except Exception:
            pass

        try:
            convert(hwp, HTML, "HTML")
        except Exception as exc:
            print(f"HTML import failed ({exc}); trying DOCX", file=sys.stderr)
            convert(hwp, DOCX, "DOCX")

        text = hwp.GetTextFile("TEXT", "") or ""
        if len(text.strip()) < 100:
            raise RuntimeError("imported document appears to be empty")

        insert_section_page_breaks(hwp)

        hwp.SaveAs(str(HWP), "HWP", "")
        hwp.SaveAs(str(PDF), "PDF", "")
    finally:
        try:
            hwp.Clear(1)
        except Exception:
            pass
        try:
            hwp.Quit()
        except Exception:
            pass

    for path in (HWP, PDF):
        if not path.exists() or path.stat().st_size == 0:
            sys.exit(f"conversion failed: {path}")
        print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
