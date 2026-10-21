#!/usr/bin/env python3
"""
Regression test for fdkaac's smart-padding (LPC pre/post-roll) feature.

do_smart_padding() in src/main.c inserts one AAC frame's worth of
LPC-extrapolated audio before and after the real signal (see
extrapolater.c), so the encoder gets real-ish context at both edges
instead of a hard edge. This script checks, for every profile that
enables it, that the encoded file's gapless boundaries are actually
sample-accurate: it decodes with ffmpeg and cross-correlates against
the original PCM to find where the real audio truly starts, then
diffs a window right at that boundary against the source.

Two bugs this catches if they come back:

  * A discard/splice strategy that removes the wrong encoded frame (or
    doesn't extend the signaled delay enough) shows up as a short but
    sharp region of high error right at the boundary, even when the
    overall correlation still looks fine -- a broad correlation score
    alone does not catch this, hence the local windowed check.

  * A *periodic* test tone is unsuitable for the alignment search: its
    autocorrelation has strong peaks at multiples of its own period,
    which can make the search lock onto the wrong lag with a
    deceptively high correlation score. Using low-pass-filtered noise
    (aperiodic) avoids that trap -- don't swap this back for a sine
    wave.

Not covered: AAC-ELD with SBR explicitly enabled (`-L 1`). Decoding it
requires an ffmpeg build with the nonfree libfdk-aac *decoder* enabled,
which isn't something a normal ffmpeg install has. That combination was
instead verified by hand with a small standalone harness linked
directly against libfdk-aac's own decoder (aacDecoder_* in
aacdecoder_lib.h) -- see the session notes in the commit that added
this script if you need to redo that.

A separate wrinkle for HE-AAC/HE-AACv2 (profiles 5/29): ffmpeg's
AAC+SBR decoder does not fully honor the default iTunes-compatible
delay.  fdkaac deliberately signals 481 core samples (962 output
samples) less than the full SBR delay by default, so ffmpeg's decoded
output contains 962 extra samples before the real audio.  With
--include-sbr-delay, which signals the full FDK delay, ffmpeg trims
the same file sample-accurately.  The audio itself is fine in both
cases; only the signaled delay differs.

This script therefore exercises those two profiles twice:

  * default iTunSMPB: accept lag 0 (if a future ffmpeg learns to scale
    the SBR delay) or the known ~962-sample offset, and still require
    a clean splice at the detected boundary;
  * --include-sbr-delay: require lag 0.

Only the default iTunSMPB container path is exercised here (no -G
option).  ffmpeg's edit-list (edts) path is not covered: for SBR it
uses the edit-list media timescale as skip_samples without rescaling
to the doubled decoder output rate, so -G 1/-G 2 still leave the same
offset.  AAC-LC handles edts correctly, but that container path is
outside this script's scope.

Usage:
    python3 tests/verify_smart_padding.py [--fdkaac PATH]

Requires: a built `fdkaac` binary, `ffmpeg`/`ffprobe` on PATH, numpy.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np

LPC_ORDER = 32

# (profile, extra fdkaac args, bitrate, channels)
PROFILES = [
    (2, [], 128, 1),    # AAC-LC
    (5, [], 64, 2),     # HE-AAC (SBR)
    (29, [], 32, 2),    # HE-AAC v2 (SBR + PS)
    (23, [], 64, 1),    # AAC-LD
    (39, [], 64, 1),    # AAC-ELD (SBR off, the default)
]

# frame length per profile, needed to size the edge-case test lengths
FRAME_LENGTH = {2: 1024, 5: 2048, 29: 2048, 23: 512, 39: 512}

# HE-AAC/HE-AACv2 (dual-rate SBR).  fdkaac's default signaling omits
# the extra SBR delay of 481 core samples (962 output samples), which
# ffmpeg then leaves as a head offset.  --include-sbr-delay signals the
# full delay instead.  See the module docstring.
SBR_PROFILES = {5, 29}
SBR_DEFAULT_LAG = 481 << 1  # 962 output samples for dual-rate SBR
SBR_LAG_TOLERANCE = 2       # observed offsets are 961 or 962

# Minimum overlap for the alignment search.  Using a shorter segment
# for short inputs lets the one-frame SBR case find the ~962-sample
# offset; the old fixed 2000-sample window could only search 48 lags.
MIN_ALIGN_OVERLAP = 512


def gen_noise(n, seed, channels=1):
    """Aperiodic, band-limited test signal. See module docstring for why
    this must not be a periodic tone."""
    if n == 0:
        return np.zeros((0, channels), dtype=np.int16)
    rng = np.random.RandomState(seed)
    noise = rng.uniform(-1, 1, n + 50)
    kernel = np.ones(8) / 8
    filtered = np.convolve(noise, kernel, mode="valid")[:n]
    filtered = filtered / np.max(np.abs(filtered)) * 8000
    data = filtered.astype(np.int16)
    if channels == 2:
        data = np.repeat(data[:, None], 2, axis=1).astype(np.int16)
    return data.reshape(n, channels)


def write_raw(path, samples):
    samples.astype("<i2").tofile(path)


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          **kw)


def encode(fdkaac, src_path, out_path, profile, extra_args, bitrate, channels):
    cmd = [fdkaac, "-p", str(profile), "-b", str(bitrate), *extra_args,
           "-R", "--raw-channels", str(channels), "--raw-rate", "44100",
           "-o", out_path, src_path]
    r = run(cmd)
    if r.returncode != 0:
        raise RuntimeError("fdkaac failed: %s\n%s" %
                           (" ".join(cmd), r.stderr.decode(errors="replace")))


def decode(path, channels):
    with tempfile.NamedTemporaryFile(suffix=".pcm", delete=False) as f:
        pcm_path = f.name
    try:
        r = run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le",
                "-ar", "44100", "-ac", str(channels), pcm_path, "-y"])
        if r.returncode != 0:
            raise RuntimeError("ffmpeg decode failed: %s" %
                               r.stderr.decode(errors="replace"))
        data = np.fromfile(pcm_path, dtype="<i2").astype(np.float64)
    finally:
        os.unlink(pcm_path)
    if channels == 2:
        data = data.reshape(-1, 2)[:, 0]
    return data


def find_head_lag(decoded, orig_mono, maxlag=6000, window=2000,
                  min_overlap=MIN_ALIGN_OVERLAP):
    """Cross-correlate to find where orig_mono best matches within
    decoded[]. Returns (lag, correlation).

    For short inputs, compare only the available overlap instead of
    refusing to search lags beyond len(decoded) - window.  Without
    that, a 2048-sample HE-AAC decode can only search 48 lags and
    cannot see the expected ~962-sample SBR offset."""
    min_overlap = min(min_overlap, len(decoded), len(orig_mono))
    best_lag, best_score = 0, -1.0
    max_lag = min(maxlag, max(0, len(decoded) - min_overlap))
    for lag in range(0, max_lag + 1):
        seg = decoded[lag:lag + window]
        overlap = min(len(seg), len(orig_mono))
        if overlap < min_overlap:
            continue
        seg = seg[:overlap]
        ref = orig_mono[:overlap]
        seg_norm = np.linalg.norm(seg)
        ref_norm = np.linalg.norm(ref)
        if seg_norm < 1 or ref_norm < 1:
            continue
        score = np.dot(seg, ref) / (seg_norm * ref_norm + 1e-9)
        if score > best_score:
            best_score, best_lag = score, lag
    return best_lag, best_score


def local_error_ratios(decoded, orig_mono, lag, win=32, nwin=15):
    """RMS error, relative to signal RMS, in consecutive windows right at
    the alignment boundary -- catches a short splice glitch that a
    broad correlation score would average away."""
    ratios = []
    for start in range(0, win * nwin, win):
        seg_d = decoded[lag + start:lag + start + win]
        seg_o = orig_mono[start:start + win]
        if len(seg_d) < win or len(seg_o) < win:
            break
        err = seg_d - seg_o
        ratios.append(np.sqrt(np.mean(err ** 2)) /
                      (np.sqrt(np.mean(seg_o ** 2)) + 1e-9))
    return ratios


class Failure(Exception):
    pass


def check_case(fdkaac, tmpdir, profile, extra_args, bitrate, channels,
               n_frames_desc, n, seed, allowed_lags=(0,), lag_tolerance=0,
               glitch_ratio_threshold=0.6):
    src = gen_noise(n, seed, channels)
    src_mono = src[:, 0].astype(np.float64)
    src_path = os.path.join(tmpdir, "src.raw")
    out_path = os.path.join(tmpdir, "out.m4a")
    write_raw(src_path, src)

    encode(fdkaac, src_path, out_path, profile, extra_args, bitrate, channels)

    if n == 0:
        return  # nothing more to verify for empty input beyond "didn't crash"

    r = run(["ffprobe", "-v", "error", "-i", out_path])
    if r.returncode != 0:
        raise Failure("ffprobe could not even open the output: %s" %
                      r.stderr.decode(errors="replace"))

    decoded = decode(out_path, channels)

    if n < 2 * LPC_ORDER:
        # too short for LPC context: extrapolater falls back to silent
        # pad, so there's nothing meaningful to align against -- just
        # confirm it decodes without crashing (already done above).
        return

    is_sbr = profile in SBR_PROFILES
    lag, corr = find_head_lag(decoded, src_mono,
                              maxlag=10000 if is_sbr else 6000)
    if corr < 0.5:
        raise Failure("no confident alignment found at all (best "
                      "corr=%.3f at lag=%d)" % (corr, lag))
    if not any(abs(lag - expected) <= lag_tolerance
               for expected in allowed_lags):
        expected_desc = str(list(allowed_lags))
        if lag_tolerance:
            expected_desc += " (+/-%d)" % lag_tolerance
        raise Failure("head misaligned: expected lag in %s, found lag=%d "
                      "(corr=%.3f)" % (expected_desc, lag, corr))

    # SBR's own lossy reconstruction has a noticeably higher per-window
    # noise floor even in known-good output (observed up to ~0.8 in
    # an isolated window during manual verification), so it needs a
    # looser threshold than the non-SBR profiles. The splice-type bugs
    # this check exists to catch look nothing like "one noisy window" --
    # they showed near-total decorrelation (ratio ~= 1.0) across several
    # *consecutive* windows, which either threshold still catches.
    threshold = 0.9 if is_sbr else glitch_ratio_threshold
    ratios = local_error_ratios(decoded, src_mono, lag)
    bad = [r for r in ratios if r > threshold]
    if bad:
        raise Failure("glitch detected right at the boundary: local error "
                      "ratios %s (threshold %.2f)" %
                      (["%.2f" % r for r in ratios], threshold))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fdkaac", default=None,
                    help="path to the fdkaac binary "
                         "(default: search build/fdkaac, ./fdkaac, PATH)")
    ap.add_argument("--keep-tmp", action="store_true",
                    help="keep temporary files for inspection on failure")
    args = ap.parse_args()

    fdkaac = args.fdkaac
    if not fdkaac:
        here = os.path.dirname(os.path.abspath(__file__))
        for candidate in (os.path.join(here, "..", "build", "fdkaac"),
                         os.path.join(here, "..", "fdkaac"),
                         shutil.which("fdkaac")):
            if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                fdkaac = candidate
                break
    if not fdkaac:
        print("error: could not find an fdkaac binary; pass --fdkaac PATH",
              file=sys.stderr)
        return 1
    fdkaac = os.path.abspath(fdkaac)

    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            print("error: %s not found on PATH" % tool, file=sys.stderr)
            return 1

    print("using fdkaac: %s" % fdkaac)

    failures = []
    passed = 0
    for profile, extra_args, bitrate, channels in PROFILES:
        flen = FRAME_LENGTH[profile]
        cases = [
            ("empty", 0),
            ("tiny (< 2*LPC_ORDER)", 10),
            ("one frame", flen),
            ("exact multiple", flen * 4),
            ("short tail (< 2*LPC_ORDER)", flen * 4 + 20),
            ("general/long", flen * 17 + 777),
        ]
        if profile in SBR_PROFILES:
            variants = [
                ("default", extra_args,
                 (0, SBR_DEFAULT_LAG), SBR_LAG_TOLERANCE),
                ("include-sbr-delay",
                 extra_args + ["--include-sbr-delay"], (0,), 0),
            ]
        else:
            variants = [(None, extra_args, (0,), 0)]

        for desc, n in cases:
            for (variant_name, variant_args,
                 allowed_lags, lag_tolerance) in variants:
                tmpdir = tempfile.mkdtemp(prefix="fdkaac-test-")
                if variant_name:
                    label = "profile=%d [%s] %s n=%d" % (
                        profile, variant_name, desc, n)
                else:
                    label = "profile=%d %s n=%d" % (profile, desc, n)
                try:
                    check_case(fdkaac, tmpdir, profile, variant_args, bitrate,
                              channels, desc, n, seed=1000 + n,
                              allowed_lags=allowed_lags,
                              lag_tolerance=lag_tolerance)
                    print("PASS: %s" % label)
                    passed += 1
                except Failure as e:
                    print("FAIL: %s -- %s" % (label, e))
                    failures.append(label)
                except RuntimeError as e:
                    print("ERROR: %s -- %s" % (label, e))
                    failures.append(label)
                finally:
                    if args.keep_tmp and failures and failures[-1] == label:
                        print("  (kept temp dir: %s)" % tmpdir)
                    else:
                        shutil.rmtree(tmpdir, ignore_errors=True)

    print()
    print("%d passed, %d failed" % (passed, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
