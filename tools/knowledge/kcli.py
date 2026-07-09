#!/usr/bin/env python3
"""kcli — inspect and query the RTG Knowledge Store from the shell.

This is the "future analyzers consume previous measurements" surface: it reads
what earlier tools recorded, without re-analyzing any audio.

  kcli.py stats
  kcli.py subjects [--kind reference]
  kcli.py record  <kind> <name> <metric> <value> [--unit U] [--window W] [--tool T]
  kcli.py history <subject_id> <metric>
  kcli.py latest  <subject_id> <metric>
  kcli.py query   [--subject ID] [--metric M]
  kcli.py compare <subjectA> <subjectB> --metric M     # diff PRIOR observations, no re-analysis
  kcli.py hypotheses [--subject ID] [--status open]
  kcli.py recon   <target_subject_id>
  kcli.py export  [--out FILE]                          # dump whole store to JSON

DB path: --db PATH, else $RTG_KNOWLEDGE_DB, else <repo>/rtg_library/knowledge.db
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kstore import KnowledgeStore, default_db_path  # noqa: E402


def _rows(rows):
    return [dict(r) for r in rows]


def _val(r):
    return json.loads(r["value_json"]) if r["value_json"] is not None else r["value"]


def main(argv=None):
    p = argparse.ArgumentParser(prog="kcli", description="Query the RTG Knowledge Store")
    p.add_argument("--db", default=None, help="store path (default: $RTG_KNOWLEDGE_DB or repo lib)")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("stats")

    s = sub.add_parser("subjects"); s.add_argument("--kind")

    s = sub.add_parser("record")
    s.add_argument("kind"); s.add_argument("name"); s.add_argument("metric")
    s.add_argument("value", type=float)
    s.add_argument("--unit"); s.add_argument("--window"); s.add_argument("--tool", default="kcli")

    s = sub.add_parser("history"); s.add_argument("subject_id", type=int); s.add_argument("metric")
    s = sub.add_parser("latest");  s.add_argument("subject_id", type=int); s.add_argument("metric")

    s = sub.add_parser("query")
    s.add_argument("--subject", type=int); s.add_argument("--metric")

    s = sub.add_parser("compare")
    s.add_argument("subject_a", type=int); s.add_argument("subject_b", type=int)
    s.add_argument("--metric", required=True)

    s = sub.add_parser("hypotheses")
    s.add_argument("--subject", type=int); s.add_argument("--status")

    s = sub.add_parser("recon"); s.add_argument("target_id", type=int)

    s = sub.add_parser("export"); s.add_argument("--out")

    args = p.parse_args(argv)
    db = args.db or default_db_path()
    with KnowledgeStore(db) as ks:
        if args.cmd == "stats":
            for k, v in ks.stats().items():
                print(f"  {k:28} {v}")

        elif args.cmd == "subjects":
            for r in ks.find_subject(kind=args.kind):
                print(f"  #{r['id']:<4} {r['kind']:<10} {r['name']}"
                      + (f"  seed={r['seed']}" if r['seed'] is not None else ""))

        elif args.cmd == "record":
            sid = ks.subject(args.kind, args.name)
            oid = ks.record(sid, args.metric, args.value, unit=args.unit,
                            window=args.window, tool=args.tool)
            r = ks.db.execute("SELECT confidence FROM observations WHERE id=?", (oid,)).fetchone()
            print(f"recorded obs #{oid} on subject #{sid}  (confidence {r['confidence']})")

        elif args.cmd == "history":
            for r in ks.history(args.subject_id, args.metric):
                print(f"  {r['created_at']}  {_val(r)}  (conf {r['confidence']}, {r['tool']})")

        elif args.cmd == "latest":
            r = ks.latest(args.subject_id, args.metric)
            print("  (none)" if not r else
                  f"  {_val(r)} {r['unit'] or ''}  conf {r['confidence']}  [{r['window'] or 'full'}] {r['tool']}")

        elif args.cmd == "query":
            for r in ks.observations(subject_id=args.subject, metric=args.metric):
                print(f"  subj#{r['subject_id']}  {r['metric']:<32} {_val(r)}  "
                      f"conf {r['confidence']}  [{r['window'] or 'full'}]")

        elif args.cmd == "compare":
            ra = ks.latest(args.subject_a, args.metric)
            rb = ks.latest(args.subject_b, args.metric)
            if not ra or not rb:
                print("  need a prior observation of that metric on BOTH subjects "
                      f"(a={'ok' if ra else 'missing'}, b={'ok' if rb else 'missing'})")
                return 1
            va, vb = _val(ra), _val(rb)
            print(f"  metric: {args.metric}")
            print(f"    subject #{args.subject_a}: {va}  (conf {ra['confidence']})")
            print(f"    subject #{args.subject_b}: {vb}  (conf {rb['confidence']})")
            if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
                print(f"    delta (a-b): {va - vb:+.4g}   "
                      f"min-confidence of comparison: {min(ra['confidence'], rb['confidence'])}")

        elif args.cmd == "hypotheses":
            for r in ks.hypotheses(subject_id=args.subject, status=args.status):
                print(f"  #{r['id']} [{r['status']}] subj#{r['subject_id']} {r['aspect']}: "
                      f"{r['statement']}  (conf {r['confidence']})")

        elif args.cmd == "recon":
            best = ks.best_reconstruction(args.target_id)
            print(f"  attempts: {len(ks.reconstructions(args.target_id))}")
            if best:
                print(f"  best: recon #{best['id']}  distance {best['distance']} "
                      f"({best['distance_metric']})  success={bool(best['success'])}")

        elif args.cmd == "export":
            dump = {
                "subjects": _rows(ks.db.execute("SELECT * FROM subjects").fetchall()),
                "observations": _rows(ks.db.execute("SELECT * FROM observations").fetchall()),
                "hypotheses": _rows(ks.db.execute("SELECT * FROM hypotheses").fetchall()),
                "reconstructions": _rows(ks.db.execute("SELECT * FROM reconstructions").fetchall()),
            }
            text = json.dumps(dump, indent=2)
            if args.out:
                with open(args.out, "w", encoding="utf-8") as f:
                    f.write(text)
                print(f"exported {sum(len(v) for v in dump.values())} rows -> {args.out}")
            else:
                print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
