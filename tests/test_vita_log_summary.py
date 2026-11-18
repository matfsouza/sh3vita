"""tools/vita_log_summary.py: resumo do sh3log.txt de um teste no Vita real (M3 Task 5)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import vita_log_summary as vls  # noqa: E402

LOG = """\
[mem] guest memory: 200 MB obtained
[d3d8] CreateDevice 790x448 fmt=21 windowed=0 depth=1/75 flags=80
[perf] 31.2 ms/frame; frame body 20.00 ms of which game code 12.00 ms and GL 8.00 ms
[perf] frames > 36 ms: 4 of 120, longest 80.0 ms
[stats] presents=1300 draws=1 clears=1 programs=9 heap=32 MB (peak 33 MB)
[perf] 33.0 ms/frame; frame body 21.00 ms of which game code 13.00 ms and GL 8.00 ms
[perf] frames > 36 ms: 10 of 120, longest 95.5 ms
[movie] 768x432, 29.97 i/s, 192.0 s, audio=1
[movie] t=2.0 s: 29.0 frames decoded/s (0.0 B skipped), 28.0 delivered/s; per second: decode 400 ms (video 380, audio 20, file read 5), conversion 90 ms, Receive 30 ms
FATAL: unhandled instruction 'xyz' at 0x00401000
ret? 0x00402000
"""


def test_summary_has_memory_frames_perf_movie_and_fatal():
    s = vls.summarize(LOG)
    assert s["memory_mb"] == 200
    assert s["presents"] == 1300
    assert s["ms_per_frame"] == [31.2, 33.0]
    assert s["slow_frames"] == (14, 240) and s["longest_ms"] == 95.5
    assert s["movie_decode_ms_per_s"] == [400.0]
    assert s["fatal"].startswith("FATAL: unhandled instruction") and "ret? 0x00402000" in s["fatal"]


def test_clean_log_has_no_fatal_and_text_report_is_portuguese():
    s = vls.summarize("[mem] guest memory: 256 MB obtained\n[stats] presents=50 draws=1\n")
    assert s["fatal"] is None and s["ms_per_frame"] == []
    text = vls.report(s)
    assert "Memória: 256 MB" in text and "Sem FATAL" in text
