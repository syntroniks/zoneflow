#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyyaml>=6,<7"]
# ///
"""Fault injection and corner analysis of the ZoneFlow fail-safe circuits.

    uv run hardware/sim/failsafe/failsafe.py faults     # every single fault
    uv run hardware/sim/failsafe/failsafe.py corners    # tolerance, temperature, supply
    uv run hardware/sim/failsafe/failsafe.py bridges    # bridges onto the keep-alive net
    uv run hardware/sim/failsafe/failsafe.py compare    # against the design document's own table
    uv run hardware/sim/failsafe/failsafe.py deck K1.coil.open crash-low   # one deck, to read

The circuit is built from `hardware/netlist.yaml` (see model.py), so the
analysis follows the netlist. Results go to `results/`. Needs ngspice.
See README.md in this directory for what the runs do and do not show.
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as R  # noqa: E402
from faults import REMARKS, Fault, all_faults  # noqa: E402
from model import NETLIST, Params, load_netlist, pin_id  # noqa: E402

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"
STOPS = ("low", "high", "hiz")
# Release times the design promises after the last edge (design section 19.4):
# gate drive gone and relay open within 0.95 s, 1.4 s if the pin stops high.
CONTRACT = {"low": 0.95, "high": 1.4, "hiz": 0.95}


def ka_pullup(comps: dict) -> bool:
    """GPIO39 (MTCK) has its weak pull-up on after reset; GPIO38 has none."""
    key = next(k for k, net in comps["U4"]["pins"].items() if net == "KA")
    return "IO39" in key


# T0: self-test with the fault there from power-up. Tm: the fault appears
# while the valve runs. Cm: crash runs with the fault appearing during the
# run, one for each way the pin can stop. A fault that is there from
# power-up and a crash is the same end state as Cm or keeps the valve from
# running at all, which T0 shows.
RUNS = ["T0", "Tm"] + [f"Cm-{stop}" for stop in STOPS]


def run_one(comps: dict, base: Params, f: Fault, tag: str, learned: tuple[float, float] | None = None):
    """One ngspice run of one fault. T: self-test, C: crash; 0: the fault is
    there from power-up, m: it appears while the valve runs."""
    p = replace(base, ac=base.ac or f.ac, ext={**base.ext, **f.ext}, usb=f.usb)
    pull = ka_pullup(comps)
    flags = {"welded": f.welded, "shorted_output": f.shorted_output}
    crash_flags = flags | {"pullup": f.fid != "R93.open"}
    try:
        if tag[0] == "T":
            tf = 0.0 if tag == "T0" else R.T_FAULT_IN_RUN
            tr = R.simulate(comps, p, f.prims, tf, R.selftest_stimulus(pull), R.T_SELFTEST_END)
            got = None if tr is None else R.read_selftest(tr, p, from_time=0.0 if tag == "T0" else R.T_FAULT_IN_RUN + 0.01, learned=learned, **flags)
        else:
            kind, stop = tag.split("-")
            tf = 0.0 if kind == "C0" else R.C_FAULT_IN_RUN
            tr = R.simulate(comps, p, f.prims, tf, R.crash_stimulus(stop, ka_pullup=pull), R.C_END)
            got = None if tr is None else R.read_crash(tr, **crash_flags)
    except R.Oscillates:
        return "timeout"
    if got is not None:
        got.rerun, got.suspect = tr.rerun, tr.suspect
    return got


def summarise(res: dict) -> dict:
    f: Fault = res["fault"]
    row = {"id": f.fid, "group": f.group, "part": f.part, "fault": f.text}
    if f.by_inspection:
        row.update(valve="by inspection", lost="", found_run="", found_start="", note=f.by_inspection, unsafe="", sim="no", remark="")
        return row
    timed_out = [k for k, v in res.items() if v == "timeout"]
    res = {k: (None if v == "timeout" else v) for k, v in res.items()}
    crashes = {k: v for k, v in res.items() if k.startswith("C")}
    failed = [k for k, v in res.items() if k != "fault" and v is None and k not in timed_out]
    ok = {k: v for k, v in crashes.items() if v is not None}
    unsafe = [k for k, c in ok.items() if c.on_at_end]
    cells = []
    for stop in STOPS:
        cs = [c for k, c in ok.items() if k.endswith(stop)]
        if any(c.on_at_end for c in cs):
            cells.append("ON")
        elif not any(c.ran for c in cs) and not any(c.t_off for c in cs):
            cells.append("never on")
        else:
            cells.append(f"{max((c.t_off or 0.0) for c in cs):.2f}")
    late = [s for s, cell in zip(STOPS, cells) if cell not in ("ON", "never on") and float(cell) > CONTRACT[s] + 0.01]
    lost = []
    if any(c.led_at_end for c in ok.values()):
        lost.append("gate drive stays")
    if f.welded:
        lost.append("relay contact closed")
    elif any(c.relay_at_end for c in ok.values()):
        lost.append("relay stays closed")
    else:
        if any(c.q1_at_end for c in ok.values()):
            lost.append("Q1 stays on")
        if any(c.q2_at_end for c in ok.values()):
            lost.append("Q2 stays on")
    runs = {k: v for k, v in res.items() if k != "fault" and v is not None}
    rerun = [k for k, v in runs.items() if v.rerun]
    suspect = [k for k, v in runs.items() if v.suspect is not None]
    t0, tm = res.get("T0"), res.get("Tm")
    new_steps = R.NEW_STEPS
    first_start = t0.first(new_steps) if t0 else None
    maybe_start = t0.first(new_steps, definite=False) if t0 else None
    # the sequence as review A2 found it: without steps 2b and 5b
    old_steps = R.OLD_STEPS
    first_old = t0.first(old_steps) if t0 else None
    first_run = tm.first(("7 run", "valve current", "8 end", "9 stop")) if tm else None
    first_run_old = tm.first(("7 run", "valve current", "8 end", "9 stop (old limits)")) if tm else None
    row.update(
        valve=" / ".join(cells),
        lost=", ".join(lost),
        found_run=f"{first_run.step}: {first_run.text}" if first_run else "",
        found_start=(f"{first_start.step}: {first_start.text}" if first_start else (f"({maybe_start.step}: level not defined)" if maybe_start else "")),
        note=("simulation failed: " + " ".join(failed)) if failed else (
            "run not finished: " + " ".join(timed_out) if timed_out else ("late: " + ", ".join(late) if late else "")),
        found_start_old=f"{first_old.step}: {first_old.text}" if first_old else "",
        found_run_old=f"{first_run_old.step}: {first_run_old.text}" if first_run_old else "",
        unsafe="yes" if unsafe else "",
        sim="yes",
        solver="; ".join(x for x in ("finer step: " + " ".join(rerun) if rerun else "", "latch set with no cause: " + " ".join(suspect) if suspect else "") if x),
        remark=REMARKS.get(f.fid, ""),
        t_a="" if not t0 or t0.t_a is None else f"{t0.t_a:.2f}",
        t_b="" if not t0 or t0.t_b is None else f"{t0.t_b:.2f}",
    )
    return row


FIELDS = ["id", "group", "part", "fault", "valve", "lost", "found_run", "found_start", "found_run_old", "found_start_old", "unsafe", "note", "remark", "t_a", "t_b", "sim", "solver"]


def cmd_faults(args: argparse.Namespace) -> int:
    R.TIMEOUT = args.timeout
    comps = load_netlist(Path(args.netlist))
    base = Params()
    faults = all_faults(comps)
    if args.only:
        faults = [f for f in faults if any(f.fid.startswith(o) for o in args.only)]
    if args.ids:
        faults = [f for f in faults if f.fid in args.ids]
    if args.author_rows:
        from author_table import AUTHOR  # noqa: PLC0415

        faults = [f for f in faults if f.fid in AUTHOR or f.fid.startswith("KA~")]
    healthy = Fault("none", "reference", "", "no fault")
    faults = [healthy] + faults
    print(f"{len(faults)} faults, {sum(1 for f in faults if not f.by_inspection) * len(RUNS)} ngspice runs", flush=True)
    # The healthy board first: its release times are the values firmware
    # would have stored at commissioning.
    ref = run_one(comps, base, healthy, "T0")
    learned = (ref.t_a, ref.t_b) if ref and ref != "timeout" and ref.t_a and ref.t_b else None
    jobs = [(f, tag) for f in faults if not f.by_inspection for tag in RUNS]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(lambda j: run_one(comps, base, *j, learned=learned), jobs))
    by_fault: dict[str, dict] = {f.fid: {"fault": f} for f in faults}
    for (f, tag), value in zip(jobs, done):
        by_fault[f.fid][tag] = value
    rows = [summarise(by_fault[f.fid]) for f in faults]
    RESULTS.mkdir(exist_ok=True)
    out = RESULTS / f"{args.tag}.csv"
    if args.update:
        # Replace the rows that were run again in the existing result file.
        # For runs that hit the time limit only because the machine was busy.
        new = {r["id"]: {k: r.get(k, "") for k in FIELDS} for r in rows}
        with out.open() as fh:
            rows = [new.get(r["id"], r) for r in csv.DictReader(fh)]
    with out.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        for row in rows:
            w.writerow({k: row.get(k, "") for k in FIELDS})
    write_markdown(rows, RESULTS / f"{args.tag}.md", args.netlist, merged=args.sequence == "merged")
    n_unsafe = sum(1 for r in rows if r["unsafe"])
    n_fail = sum(1 for r in rows if r["note"].startswith("simulation failed"))
    ref = rows[0]
    print(f"reference: valve off after {ref['valve']} s (pin low / high / high-impedance), tA {ref.get('t_a')} s, tB {ref.get('t_b')} s, "
          f"self-test {'passes' if not ref['found_start'] and not ref['found_run'] else 'FAILS: ' + ref['found_start'] + ref['found_run']}")
    print(f"{len(rows) - 1} faults: {n_unsafe} leave the valve on, {n_fail} did not simulate. Written to {out}")
    print(f"solver check: {sum(1 for r in rows if 'finer step' in r.get('solver', ''))} faults had a run repeated with a finer step, "
          f"{sum(1 for r in rows if 'no cause' in r.get('solver', ''))} still show the latch setting with no cause")
    return 0


def write_markdown(rows: list[dict], path: Path, netlist: str, merged: bool = False) -> None:
    """merged: show what the self-test sequence found as it was merged in pull
    request #20, without steps 2b and 5b and with the old limits of step 9."""
    run, start = ("found_run_old", "found_start_old") if merged else ("found_run", "found_start")
    lines = [
        "<!-- Written by hardware/sim/failsafe/failsafe.py faults. Do not edit. -->",
        "",
        "The two columns on the right are for the self-test sequence " + (
            "as merged in pull request #20: no step 2b, no step 5b, the old limits in step 9. "
            "`asfound.csv` has the columns for the sequence of review A2 as well." if merged else "of design section 19.7 as review A2 leaves it."),
        "",
        "| Fault | Valve current ends after (s): pin low / high / high-impedance | Layer lost | Found while the valve runs | Found at the next start |",
        "|---|---|---|---|---|",
    ]
    group = None
    for r in rows:
        if r["group"] != group:
            group = r["group"]
            lines.append(f"| **{group}** | | | | |")
        if r["sim"] == "no":
            lines.append(f"| {r['fault']} | not simulated | | | {r['note']} |")
            continue
        valve = r["valve"].replace("ON", "**stays on**")
        note = f" ({r['note']})" if r["note"] else ""
        remark = f" *{r['remark']}*" if r.get("remark") else ""
        lines.append(f"| {r['fault']}{remark} | {valve}{note} | {r['lost']} | {r[run].replace(' (old limits)', '')} | {r[start].replace(' (old limits)', '')} |")
    path.write_text("\n".join(lines) + "\n")


def cmd_compare(args: argparse.Namespace) -> int:
    """Set a result file against what the design document said (author_table.py)."""
    from author_table import AUTHOR  # noqa: PLC0415

    with (RESULTS / f"{args.tag}.csv").open() as fh:
        rows = {r["id"]: r for r in csv.DictReader(fh)}
    said = {"": "none", "A": "gate drive (detector A) stuck on", "B": "relay path (detector B, Q1, Q2 or K1) stuck on", "AB": "both"}
    groups = {"3 start": ("3 start", "4 expander"), "6 relay": ("6 relay", "7 run"), "7 run": ("6 relay", "7 run")}
    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py compare. Do not edit. -->", "",
             "| Fault | Design document: stuck on | Simulation: stuck on | Design document: found by | Simulation: found by | |", "|---|---|---|---|---|---|"]
    differ = 0
    for fid, (layer, test) in AUTHOR.items():
        r = rows.get(fid)
        if r is None:
            continue
        lost = [x.strip() for x in r["lost"].split(",") if x.strip()]
        # A stuck detector A keeps the expander out of reset, so Q1 stays on
        # with it: "gate drive stays, Q1 stays on" is the document's "A".
        # Q1 alone staying on is a fault of the relay path, the document's
        # "B", like Q2 or the relay itself.
        a = "gate drive stays" in lost
        b = any(x in lost for x in ("Q2 stays on", "relay stays closed", "relay contact closed")) or ("Q1 stays on" in lost and not a)
        sim = ("A" if a else "") + ("B" if b else "")
        found = list(dict.fromkeys(x.split(":")[0].replace(" (old limits)", "") for x in (r["found_run_old"], r["found_start_old"]) if x))
        if test is None:
            same_test = True
        elif test == "any":
            same_test = bool(found)
        elif test == "":
            same_test = not found
        else:
            same_test = any(x in groups.get(test, (test,)) for x in found)
        verdict = "" if sim == layer and same_test and not (r["unsafe"] and layer != "AB") else "**differs**"
        differ += bool(verdict)
        sim_text = (r["lost"] or "none") + (", **valve stays on**" if r["unsafe"] else "")
        test_text = {None: "(no claim)", "": "not found", "any": "by its symptom"}.get(test, test)
        lines.append(f"| {r['fault']} | {said[layer]} | {sim_text} | {test_text} | {'; '.join(found) or 'not found'} | {verdict} |")
    lines += ["", f"{len(AUTHOR)} rows of the design document, {differ} differ."]
    out = RESULTS / f"{args.tag}-compare.md"
    out.write_text("\n".join(lines) + "\n")
    print(f"{len(AUTHOR)} rows, {differ} differ. Written to {out}")
    return 0


def cmd_deck(args: argparse.Namespace) -> int:
    comps = load_netlist(Path(args.netlist))
    f = next((x for x in all_faults(comps) if x.fid == args.fault), None) if args.fault != "none" else Fault("none", "", "", "")
    if f is None:
        print(f"no fault {args.fault}", file=sys.stderr)
        return 1
    p = replace(Params(), ac=f.ac, ext=dict(f.ext))
    pull = ka_pullup(comps)
    kind, _, stop = args.scenario.partition("-")
    mid = args.mid
    if kind == "selftest":
        stim, tend, tf = R.selftest_stimulus(pull), R.T_SELFTEST_END, R.T_FAULT_IN_RUN if mid else 0.0
    else:
        stim, tend, tf = R.crash_stimulus(stop or "low", ka_pullup=pull), R.C_END, R.C_FAULT_IN_RUN if mid else 0.0
    out = Path(args.out)
    tr = R.simulate(comps, p, f.prims, tf, stim, tend, keep=out)
    print(f"deck written to {out}; simulation {'ran' if tr else 'FAILED'}")
    if tr and kind == "selftest":
        st = R.read_selftest(tr, p, welded=f.welded, shorted_output=f.shorted_output)
        for c in st.checks:
            if c.ok is not True:
                print(f"  {c.step}: {c.text} [{'fails' if c.ok is False else 'level not defined'}]")
        print(f"  tA {st.t_a} s, tB {st.t_b} s")
    elif tr:
        print(" ", R.read_crash(tr, welded=f.welded, shorted_output=f.shorted_output))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("faults", help="run every single fault")
    a.add_argument("--netlist", default=str(NETLIST))
    a.add_argument("--tag", default="faults", help="name of the result files")
    a.add_argument("--only", nargs="*", help="fault id prefixes")
    a.add_argument("--ids", nargs="*", help="exact fault ids")
    a.add_argument("--author-rows", action="store_true", help="only the faults the design document's own table names")
    a.add_argument("--update", action="store_true", help="with --ids or --only: replace those rows in the existing result file and keep the rest")
    a.add_argument("--timeout", type=int, default=R.TIMEOUT, help="seconds of computing allowed for one run")
    a.add_argument("--sequence", choices=("a2", "merged"), default="a2",
                   help="which self-test sequence the two 'found' columns of the markdown table show: review A2's, or the one merged in pull request #20")
    a.add_argument("--jobs", type=int, default=os.cpu_count())
    a.set_defaults(fn=cmd_faults)
    c = sub.add_parser("compare", help="compare a result file with the design document's own fault table")
    c.add_argument("--tag", default="asfound")
    c.set_defaults(fn=cmd_compare)
    d = sub.add_parser("deck", help="write and run one deck")
    d.add_argument("fault")
    d.add_argument("scenario", help="selftest, crash-low, crash-high or crash-hiz")
    d.add_argument("--mid", action="store_true", help="the fault appears while the valve runs")
    d.add_argument("--netlist", default=str(NETLIST))
    d.add_argument("--out", default="deck.cir")
    d.set_defaults(fn=cmd_deck)
    import bridges  # noqa: PLC0415
    import commission  # noqa: PLC0415
    import corners  # noqa: PLC0415
    import duty  # noqa: PLC0415
    import floatload  # noqa: PLC0415
    import ldo  # noqa: PLC0415
    import sequence  # noqa: PLC0415
    import warmup  # noqa: PLC0415

    corners.register(sub)
    bridges.register(sub)
    duty.register(sub)
    warmup.register(sub)
    commission.register(sub)
    ldo.register(sub)
    sequence.register(sub)
    floatload.register(sub)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
