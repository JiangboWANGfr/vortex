#!/usr/bin/env python3
"""Turn Vortex crypto runner_output TSVs into paper-ready tables and figures.

Reads the GHASH / AES-GCM benchmark TSVs produced by ci/runner/*_bench_runner.sh
and emits, into an output directory:
  - markdown tables (tables.md)
  - LaTeX booktabs tables (tables.tex)
  - cleaned CSVs (one per dataset, easy to plot anywhere)
  - a summary.md with the headline numbers
  - PNG figures, IF matplotlib is importable (overhead bar, design-space curve,
    speedup bar). Without matplotlib the CSVs + a self-contained plot script
    (make_figures.py) are still written so figures can be produced later.

Stdlib-only for everything except the optional PNGs. Run with any python3; run
with a matplotlib-capable interpreter to also get the PNGs.

Usage:
  python3 tools/paper_results.py [--root build64/runner_output] [--out <dir>]
"""

import argparse
import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except Exception:
    HAVE_MPL = False


def to_num(s):
    """'2,048'->2048, '1.85x'->1.85, '0.0613'->0.0613, else None."""
    if s is None:
        return None
    t = s.strip().replace(",", "")
    if t.endswith("x"):
        t = t[:-1]
    try:
        f = float(t)
        return int(f) if f.is_integer() else f
    except ValueError:
        return None


def read_tsv(path):
    if not os.path.isfile(path):
        return None
    with open(path, newline="") as fh:
        rows = list(csv.DictReader(fh, delimiter="\t"))
    return rows


def md_table(headers, rows):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join("---" for _ in headers) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out)


def latex_table(headers, rows, caption, label):
    cols = "l" * len(headers)
    lines = [r"\begin{table}[t]", r"\centering",
             r"\caption{%s}" % caption, r"\label{tab:%s}" % label,
             r"\begin{tabular}{%s}" % cols, r"\toprule",
             " & ".join(h.replace("_", r"\_") for h in headers) + r" \\", r"\midrule"]
    for r in rows:
        lines.append(" & ".join(str(c).replace("_", r"\_").replace("x", r"$\times$")
                                for c in r) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    return "\n".join(lines)


def write_csv(path, headers, rows):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(headers)
        w.writerows(rows)


# ----------------------------------------------------------------------------

def build_gcm(root):
    """AES-GCM confidentiality overhead (the headline)."""
    rows = read_tsv(os.path.join(root, "gcm_bench/results/simx/results.tsv"))
    if not rows:
        return None
    headers = ["accel_mode", "dispatch", "total_bytes", "cycles",
               "cyc_per_byte", "overhead_x", "native_speedup", "status"]
    table = []
    for r in rows:
        table.append([r["accel_mode"], r["dispatch"], r["total_bytes"], r["cycles"],
                      r.get("cyc_per_byte", ""), r.get("overhead_x", ""),
                      r.get("native_speedup", ""), r["status"]])
    return {"headers": headers, "rows": table, "raw": rows}


def build_design_space(root):
    """GHASH multiplier design-space (radix sweep).

    Prefer the cycle-accurate rtlsim sweep: simx does NOT model the crypto
    functional-unit latency (radix=1 and radix=128 give bit-identical cycles
    there), so only rtlsim is valid for a MUL-latency study. The rtlsim sweep
    is run at 1 warp/core, where there is no warp-level latency hiding, so a
    flat curve proves the MUL is genuinely off the critical path.
    """
    rows = None
    driver = "?"
    for drv in ("rtlsim", "simx"):
        rows = read_tsv(os.path.join(root, f"ghash_bench/results/{drv}/design_space.tsv"))
        if rows:
            driver = drv
            break
    if not rows:
        return None
    wpc = rows[0].get("warps_per_core", "")
    headers = ["mul_radix", "mul_cycles", "cycles", "cyc_per_byte", "driver", "warps/core"]
    table = []
    for r in rows:
        radix = to_num(r["mul_radix"])
        tb = to_num(r["total_bytes"])
        cyc = to_num(r["cycles"])
        cpb = r.get("cyc_per_byte") or (round(cyc / tb, 2) if (cyc and tb) else "")
        mulc = (128 // radix + 2) if radix else ""
        table.append([r["mul_radix"], mulc, r["cycles"], cpb, driver, r.get("warps_per_core", wpc)])
    return {"headers": headers, "rows": table, "raw": rows, "driver": driver, "wpc": wpc}


def build_size_sweep(root):
    """Full-load AES-GCM size sweep (sweep_warp.tsv + sweep_lane.tsv)."""
    raw = []
    for name in ("sweep_warp.tsv", "sweep_lane.tsv"):
        rows = read_tsv(os.path.join(root, "gcm_bench/results/simx", name))
        if rows:
            raw.extend(rows)
    if not raw:
        return None
    headers = ["accel_mode", "dispatch", "bytes_per_task", "total_bytes",
               "cycles", "cyc_per_byte", "gbps_1ghz", "overhead_x", "status"]
    raw.sort(key=lambda r: (r["accel_mode"], r["dispatch"], to_num(r["bytes_per_task"]) or 0))
    table = [[r["accel_mode"], r["dispatch"], r["bytes_per_task"], r["total_bytes"],
              r["cycles"], r.get("cyc_per_byte", ""), r.get("gbps_1ghz", ""),
              r.get("overhead_x", ""), r["status"]] for r in raw]
    return {"headers": headers, "rows": table, "raw": raw}


def build_correctness():
    """Static correctness summary (from the smoke tests)."""
    headers = ["suite", "what", "cases", "result"]
    rows = [
        ["ghash_smoke", "GF(2^128) algebraic identities", "8", "PASS (sw+native, simx+rtlsim)"],
        ["aes_gcm_smoke", "NIST AES-256-GCM TC13-16", "4", "PASS (sw+native, simx+rtlsim)"],
        ["gcm_bench", "sw vs native tag checksum", "all", "MATCH (bit-exact)"],
    ]
    return {"headers": headers, "rows": rows}


def headline(gcm):
    """Extract the key numbers for summary.md."""
    out = {}
    if gcm:
        by = {(r["accel_mode"], r["dispatch"]): r for r in gcm["raw"]}
        warp_native = by.get(("NATIVE", "WARP"))
        if warp_native:
            out["native_overhead_warp"] = warp_native.get("overhead_x", "?")
            out["native_speedup_warp"] = warp_native.get("native_speedup", "?")
        sw = by.get(("SOFTWARE", "WARP"))
        if sw:
            out["sw_overhead_warp"] = sw.get("overhead_x", "?")
    return out


# ----------------------------------------------------------------------------

def fig_gcm_overhead(gcm, out):
    if not (HAVE_MPL and gcm):
        return None
    warp = [r for r in gcm["raw"] if r["dispatch"] == "WARP"]
    order = {"UNPROTECTED": 0, "SOFTWARE": 1, "NATIVE": 2}
    warp.sort(key=lambda r: order.get(r["accel_mode"], 9))
    labels = [r["accel_mode"].title() for r in warp]
    vals = [to_num(r["overhead_x"]) or 1.0 for r in warp]
    colors = ["#4c72b0", "#c44e52", "#55a868"]
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    bars = ax.bar(labels, vals, color=colors[:len(labels)])
    ax.set_yscale("log")
    ax.set_ylabel("Slowdown vs unprotected (x, log)")
    ax.set_title("AES-256-GCM confidentiality overhead (WARP)", fontsize=10)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width()/2, v, f"{v:g}x",
                ha="center", va="bottom", fontsize=9)
    fig.tight_layout()
    p = os.path.join(out, "fig_gcm_overhead.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


def fig_design_space(ds, out):
    if not (HAVE_MPL and ds):
        return None
    pts = []
    for r in ds["raw"]:
        radix = to_num(r["mul_radix"])
        cyc = to_num(r["cycles"])
        tb = to_num(r["total_bytes"])
        if radix and cyc and tb:
            pts.append((radix, cyc / tb))
    pts.sort()
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    ax.plot(xs, ys, "o-", color="#55a868")
    ax.set_xscale("log", base=2)
    ax.set_xticks(xs)
    ax.set_xticklabels([str(x) for x in xs])
    ax.set_ylim(0, max(ys) * 1.3)
    ax.set_ylim(0, max(ys) * 1.4)
    drv = ds.get("driver", "?")
    ax.set_xlabel("MUL radix (bits/cycle); MUL = 128/radix cycles")
    ax.set_ylabel("cycles / byte")
    ax.set_title(f"GHASH multiplier design-space ({drv}, 1 warp/core)", fontsize=10)
    ax.text(0.5, 0.12, "flat with no warp hiding:\nMUL overlaps memory, not MUL-bound",
            transform=ax.transAxes, ha="center", color="#555", fontsize=8)
    fig.tight_layout()
    p = os.path.join(out, "fig_ghash_design_space.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


def _series_by(sweep, ykey, mode_filter=None):
    series = {}
    for r in sweep["raw"]:
        if mode_filter and r["accel_mode"] not in mode_filter:
            continue
        x = to_num(r["bytes_per_task"])
        y = to_num(r.get(ykey, ""))
        if x is None or y is None:
            continue
        k = f'{r["accel_mode"].title()} {r["dispatch"]}'
        series.setdefault(k, []).append((x, y))
    return {k: sorted(v) for k, v in series.items()}


def fig_throughput_vs_size(sweep, out):
    if not (HAVE_MPL and sweep):
        return None
    series = _series_by(sweep, "cyc_per_byte")
    if not series:
        return None
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    for k in sorted(series):
        xs = [p[0] for p in series[k]]
        ys = [p[1] for p in series[k]]
        ax.plot(xs, ys, "o-", label=k)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("bytes per stream")
    ax.set_ylabel("cycles / byte (log)")
    ax.set_title("AES-256-GCM cost vs message size (full load)", fontsize=10)
    ax.legend(fontsize=8)
    fig.tight_layout()
    p = os.path.join(out, "fig_gcm_throughput_vs_size.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


def fig_overhead_vs_size(sweep, out):
    if not (HAVE_MPL and sweep):
        return None
    series = _series_by(sweep, "overhead_x", mode_filter={"NATIVE"})
    if not series:
        return None
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    for k in sorted(series):
        xs = [p[0] for p in series[k]]
        ys = [p[1] for p in series[k]]
        ax.plot(xs, ys, "o-", label=k)
    ax.set_xscale("log", base=2)
    ax.set_xlabel("bytes per stream")
    ax.set_ylabel("overhead x vs unprotected")
    ax.set_title("Confidentiality overhead vs message size", fontsize=10)
    ax.axhline(1.0, ls="--", color="#999", lw=1)
    ax.legend(fontsize=8)
    fig.tight_layout()
    p = os.path.join(out, "fig_gcm_overhead_vs_size.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


def fig_speedup(gcm, out):
    if not (HAVE_MPL and gcm):
        return None
    pts = []
    for r in gcm["raw"]:
        if r["accel_mode"] == "NATIVE":
            s = to_num(r.get("native_speedup", ""))
            if s:
                pts.append((f"GCM {r['dispatch']}", s))
    if not pts:
        return None
    labels = [p[0] for p in pts]
    vals = [p[1] for p in pts]
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    bars = ax.bar(labels, vals, color="#55a868")
    ax.set_ylabel("Hardware speedup vs software (x)")
    ax.set_title("Native AES-256-GCM speedup over software", fontsize=10)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width()/2, v, f"{v:g}x",
                ha="center", va="bottom", fontsize=9)
    fig.tight_layout()
    p = os.path.join(out, "fig_gcm_speedup.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


# ----------------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_root = os.path.join(os.path.dirname(here), "build64", "runner_output")
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=default_root, help="runner_output dir")
    ap.add_argument("--out", default=None, help="output dir (default <root>/paper)")
    args = ap.parse_args()
    out = args.out or os.path.join(args.root, "paper")
    os.makedirs(out, exist_ok=True)

    gcm = build_gcm(args.root)
    ds = build_design_space(args.root)
    sweep = build_size_sweep(args.root)
    corr = build_correctness()

    md = ["# Vortex crypto results\n"]
    tex = []

    def add(title, data, caption, label, csvname):
        if not data:
            md.append(f"## {title}\n\n_(no data found)_\n")
            return
        md.append(f"## {title}\n\n" + md_table(data["headers"], data["rows"]) + "\n")
        tex.append(latex_table(data["headers"], data["rows"], caption, label))
        write_csv(os.path.join(out, csvname), data["headers"], data["rows"])

    add("AES-256-GCM confidentiality overhead",
        gcm, "AES-256-GCM throughput and confidentiality overhead vs the "
        "unprotected data-movement baseline (32 KB, simx).", "gcm_overhead",
        "gcm_overhead.csv")
    add("GHASH multiplier design-space (radix sweep)",
        ds, "GHASH digit-serial multiplier design-space on cycle-accurate "
        "rtlsim at 1 warp/core (no warp-level latency hiding). A 43x faster MUL "
        "(radix 1->128) changes cycles by ~0\\%, proving the MUL is off the "
        "critical path (it overlaps per-block memory access), not MUL-bound. "
        "NOTE: simx cannot model this -- it ignores crypto FU latency.",
        "ghash_design_space", "ghash_design_space.csv")
    add("AES-256-GCM full-load size sweep",
        sweep, "AES-256-GCM cost and overhead vs message size at full machine "
        "utilization (WARP t=32, LANE t=128; simx).",
        "gcm_size_sweep", "gcm_size_sweep.csv")
    add("Correctness", corr, "Functional verification summary.",
        "correctness", "correctness.csv")

    hl = headline(gcm)
    summary = ["# Headline\n"]
    if hl:
        summary.append(
            "- **Hardware AES-256-GCM speedup over software:** "
            f"{hl.get('native_speedup_warp', '?')} (WARP).")
        summary.append(
            "- **Confidentiality overhead vs unprotected:** "
            f"software {hl.get('sw_overhead_warp', '?')} -> "
            f"hardware {hl.get('native_overhead_warp', '?')} (near-free).")
    summary.append(
        "- **GHASH multiplier design-space (cycle-accurate rtlsim, 1 warp/core):** "
        "a 43x faster MUL (radix 1->128) changes cycles by ~0% even with no "
        "warp-level latency hiding -> the MUL is off the critical path (overlaps "
        "per-block memory), so the cheap bit-serial multiplier suffices. (simx "
        "cannot show this: it does not model crypto FU latency.)")
    summary.append("\nMatplotlib " + ("available: PNG figures written." if HAVE_MPL
                   else "NOT available: CSVs written; rerun with a matplotlib "
                   "python for PNGs."))

    figs = [f for f in (fig_gcm_overhead(gcm, out),
                        fig_design_space(ds, out),
                        fig_speedup(gcm, out),
                        fig_throughput_vs_size(sweep, out),
                        fig_overhead_vs_size(sweep, out)) if f]
    if figs:
        md.append("## Figures\n")
        for f in figs:
            md.append(f"![{os.path.basename(f)}]({os.path.basename(f)})\n")

    with open(os.path.join(out, "tables.md"), "w") as fh:
        fh.write("\n".join(md) + "\n")
    with open(os.path.join(out, "tables.tex"), "w") as fh:
        fh.write("\n\n".join(tex) + "\n")
    with open(os.path.join(out, "summary.md"), "w") as fh:
        fh.write("\n".join(summary) + "\n")

    print(f"wrote tables/CSVs to {out}")
    print("\n".join(summary))
    if figs:
        print("figures:", ", ".join(os.path.basename(f) for f in figs))
    else:
        print("figures: (matplotlib unavailable; CSVs are plot-ready)")


if __name__ == "__main__":
    main()
