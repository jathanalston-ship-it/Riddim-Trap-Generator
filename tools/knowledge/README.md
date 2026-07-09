# tools/knowledge — the RTG Knowledge Store

The system's long-term memory. A durable, append-only ledger of everything RTG
observes, hypothesizes, and reconstructs, so that nothing it learns is ever
thrown away. This is the compounding-knowledge asset the master architecture
calls "the moat" (docs/architecture/01 §10) — see the full spec in
**docs/architecture/12-knowledge-store.md**.

Stdlib only (`sqlite3`); no numpy/ML deps. The canonical schema lives in
`database/migrations/001_knowledge_store.sql` and is applied automatically.

## Files

| File | Role |
|---|---|
| `kstore.py` | Python access layer — `KnowledgeStore`: subjects, observations, hypotheses, reconstructions, queries. |
| `confidence.py` | Grounded confidence priors per metric (turns EARS_AND_EYES limitations into data). |
| `kcli.py` | Shell interface — inspect/query/compare/export prior measurements without re-analyzing audio. |
| `selfcheck.py` | Dependency-free end-to-end test (the push gate for this subsystem). |

## Quick start

```bash
# measure a track and PERSIST it (earview, off by default):
python3 tools/earview/ears.py ears track.wav 145 --record --kind reference --subject Seleman.wav

# a different tool later reads what was stored — no re-analysis:
python3 tools/knowledge/kcli.py subjects
python3 tools/knowledge/kcli.py query --subject 1
python3 tools/knowledge/kcli.py compare 1 2 --metric loudness.integrated_lufs
python3 tools/knowledge/kcli.py stats
```

From Python:

```python
import sys; sys.path.insert(0, "tools/knowledge")
from kstore import KnowledgeStore
with KnowledgeStore() as ks:
    sid = ks.subject_for_file("Seleman.wav", "reference", genre="riddim")
    ks.record(sid, "loudness.integrated_lufs", -7.8, unit="LUFS", tool="ears.py", window="drop")
    print(ks.latest(sid, "loudness.integrated_lufs")["confidence"])   # 0.9
```

## Where the data lives

`$RTG_KNOWLEDGE_DB`, else `<repo>/rtg_library/knowledge.db`. The store is
**data, not source** — it is gitignored and must never be committed.

## Contract highlights

- **Append-only**: `record()` never updates or deletes; a re-measurement is a
  new row, so `history()` shows drift over time and `latest()` is the newest.
- **Confidence on every number**: defaults from `confidence.py` (robust metrics
  high, documented-degenerate metrics low, stems lift confounded metrics).
- **Provenance + lineage**: tool/version/method on every observation;
  `derived_from` links the observations a derived number consumed.
- **Hypotheses are refined, not overwritten**: `refine_hypothesis()` creates a
  new claim linked to (and superseding) the old one — the reasoning trail is
  never lost.
- **Failures are kept**: a reconstruction that missed is diagnostic data; the
  winning recipe is `best_reconstruction()` = min distance.
- **Determinism (CLAUDE.md Rule 7)**: this is metadata OUTSIDE the audio path;
  wall-clock timestamps here can never perturb a rendered sample.

## Gate

```bash
python3 tools/knowledge/selfcheck.py   # must print ALL ... CHECKS PASSED
```
