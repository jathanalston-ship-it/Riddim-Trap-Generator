#!/usr/bin/env python3
"""selfcheck — a dependency-free end-to-end test of the Knowledge Store.

Runs against a throwaway DB (no numpy/librosa/audio needed), so it can gate
every push cheaply. Exercises the full contract: subjects, append-only
observations with derived confidence, vector metrics, hypothesis refinement,
reconstruction (success + failure), and every query path. Exits non-zero on the
first failed assertion.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kstore import KnowledgeStore          # noqa: E402
from confidence import confidence_for       # noqa: E402


def check(cond, msg):
    if not cond:
        print(f"FAIL: {msg}")
        sys.exit(1)
    print(f"  ok  {msg}")


def main():
    tmp = tempfile.mkdtemp(prefix="rtg_kstore_")
    dbp = os.path.join(tmp, "knowledge.db")
    ks = KnowledgeStore(dbp)

    # -- confidence model is grounded, not uniform --------------------------
    check(confidence_for("loudness.integrated_lufs") > 0.8, "robust metric -> high confidence")
    check(confidence_for("formant.motion_median") < 0.3, "degenerate metric -> low confidence")
    check(confidence_for("onset.hat_rate", "stem:hat") >
          confidence_for("onset.hat_rate", "full"), "stem lifts confounded-metric confidence")

    # -- subjects are idempotent by identity --------------------------------
    ref = ks.subject("reference", "Seleman.wav", content_hash="deadbeef", meta={"genre": "riddim"})
    ref2 = ks.subject("reference", "Seleman.wav", content_hash="deadbeef")
    check(ref == ref2, "same (kind,name,hash) -> same subject id (accumulate, not fork)")
    other = ks.subject("render", "mine_seed42.wav", seed=42)
    check(other != ref, "different subject -> different id")

    # -- observations append with derived confidence & provenance -----------
    o1 = ks.record(ref, "loudness.integrated_lufs", -7.8, unit="LUFS", tool="ears.py",
                   window="drop", method="BS.1770 K-weighted")
    o2 = ks.record(ref, "roughness.am_gnarl", 1240.0, tool="ears.py", window="drop")
    check(ks.latest(ref, "loudness.integrated_lufs")["confidence"] > 0.8, "loudness auto-confidence high")
    check(ks.latest(ref, "roughness.am_gnarl")["confidence"] < 0.7, "roughness auto-confidence medium")

    # append-only: a second reading is a NEW row, both kept
    ks.record(ref, "loudness.integrated_lufs", -7.9, unit="LUFS", tool="ears.py", window="drop")
    check(len(ks.history(ref, "loudness.integrated_lufs")) == 2, "second reading appended, not overwritten")
    check(ks.latest(ref, "loudness.integrated_lufs")["value"] == -7.9, "latest returns newest reading")

    # vector metric round-trips through value_json
    ks.record(ref, "bark.spectrum", vector=[0.1, 0.2, 0.3], tool="ears.py")
    import json as _json
    barks = _json.loads(ks.latest(ref, "bark.spectrum")["value_json"])
    check(barks == [0.1, 0.2, 0.3], "vector metric stored/retrieved intact")

    # derived_from lineage is preserved
    od = ks.record(ref, "spectrum.tilt", -3.1, tool="analysis", derived_from=[o1, o2])
    check(_json.loads(ks.latest(ref, "spectrum.tilt")["derived_from"]) == [o1, o2],
          "derived_from lineage recorded")

    # bulk record
    ids = ks.record_many(other, {"loudness.integrated_lufs": -9.0, "bark.spectrum": [1, 2, 3]},
                         tool="engine", window="drop")
    check(len(ids) == 2, "record_many wrote both metrics")

    # -- hypotheses: refine, don't overwrite --------------------------------
    h = ks.hypothesize(ref, "growl.synth_method", {"method": "FM", "sweep": "slow"},
                       confidence=0.4, tool="cortex", evidence=[o2])
    h2 = ks.refine_hypothesis(h, statement={"method": "FM", "sweep": "fast"},
                              confidence=0.7, status="supported", evidence=[o2])
    old = ks.db.execute("SELECT status FROM hypotheses WHERE id=?", (h,)).fetchone()["status"]
    new = ks.db.execute("SELECT refined_from,confidence FROM hypotheses WHERE id=?", (h2,)).fetchone()
    check(old == "superseded", "refined hypothesis supersedes the old (trail kept)")
    check(new["refined_from"] == h and new["confidence"] == 0.7, "refinement links back & updates confidence")
    check(len(ks.hypotheses(subject_id=ref)) == 2, "both hypotheses retained")

    # -- reconstructions: keep successes AND failures -----------------------
    ks.record_reconstruction(ref, result_id=other, hypothesis_id=h2,
                             recipe={"osc": "fm", "ratio": 2.0}, distance=0.42,
                             distance_metric="ears.perceptual", success=False, tool="recon")
    ks.record_reconstruction(ref, result_id=other, hypothesis_id=h2,
                             recipe={"osc": "fm", "ratio": 1.5}, distance=0.18,
                             distance_metric="ears.perceptual", success=True, tool="recon")
    check(len(ks.reconstructions(ref)) == 2, "failed reconstruction kept as diagnostic data")
    best = ks.best_reconstruction(ref)
    check(best["distance"] == 0.18 and best["success"] == 1, "best_reconstruction = min distance")

    # -- summary ------------------------------------------------------------
    st = ks.stats()
    check(st["subjects"] == 2 and st["successful_reconstructions"] == 1, "stats consistent")

    ks.close()

    # -- reopening the SAME db preserves everything (durability) ------------
    ks2 = KnowledgeStore(dbp)
    check(ks2.stats()["observations"] == st["observations"], "store persists across reopen")
    ks2.close()

    print(f"\nALL KNOWLEDGE-STORE CHECKS PASSED  ({st['observations']} obs, "
          f"{st['subjects']} subjects, {st['hypotheses']} hypotheses)")


if __name__ == "__main__":
    main()
