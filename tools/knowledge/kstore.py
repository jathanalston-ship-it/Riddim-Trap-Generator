#!/usr/bin/env python3
"""kstore — the Python access layer for the RTG Knowledge Store.

A thin, dependency-free (stdlib sqlite3 only) API over the schema in
database/migrations/001_knowledge_store.sql. This is the memory every RTG
analyzer writes to and reads from, so that observations, hypotheses, and
reconstruction results accumulate forever instead of being thrown away after
steering a single render.

Design commitments (mirroring the product directive):
  * Append-only observations   — record() never updates or deletes.
  * Every measurement is trusted explicitly — confidence defaults from the
    grounded model in confidence.py when the caller doesn't override it.
  * Full provenance            — tool/version/method on every row.
  * Consume previous work      — derived_from links + rich query methods.
  * Nothing disappears         — hypotheses are refined, not overwritten;
    failed reconstructions are kept as diagnostic data.

Determinism (CLAUDE.md Rule 7): this is metadata OUTSIDE the audio path. It uses
wall-clock time and autoincrement ids and can never perturb rendered samples.

Usage:
    from kstore import KnowledgeStore
    with KnowledgeStore() as ks:
        sid = ks.subject_for_file("Seleman.wav", "reference", genre="riddim")
        ks.record(sid, "loudness.integrated_lufs", -7.8, unit="LUFS",
                  tool="ears.py", window="drop")
        print(ks.latest(sid, "loudness.integrated_lufs"))
"""
from __future__ import annotations

import json
import os
import sqlite3
import time
import hashlib

try:
    from confidence import confidence_for
except ImportError:  # allow `import kstore` from another directory
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from confidence import confidence_for

SCHEMA_VERSION = 1
_MIGRATION = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "..", "database", "migrations", "001_knowledge_store.sql")
)


def default_db_path() -> str:
    """Where the store lives. Overridable with RTG_KNOWLEDGE_DB.

    Defaults under the repo's rtg_library/ (already gitignored) so observations
    are never accidentally committed — they are data, not source.
    """
    env = os.environ.get("RTG_KNOWLEDGE_DB")
    if env:
        return env
    repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    return os.path.join(repo, "rtg_library", "knowledge.db")


def _now() -> str:
    # ISO-8601 UTC, second precision. Metadata only — see Rule 7 note above.
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha1_of_file(path: str, limit: int | None = None) -> str | None:
    """SHA-1 of a file's bytes (whole file, or first `limit` bytes)."""
    try:
        h = hashlib.sha1()
        with open(path, "rb") as f:
            if limit is None:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            else:
                h.update(f.read(limit))
        return h.hexdigest()
    except OSError:
        return None


class KnowledgeStore:
    def __init__(self, path: str | None = None):
        self.path = path or default_db_path()
        parent = os.path.dirname(self.path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self.db = sqlite3.connect(self.path)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA foreign_keys = ON")
        self._migrate()

    # -- lifecycle ----------------------------------------------------------
    def _migrate(self):
        row = self.db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_migrations'"
        ).fetchone()
        applied = set()
        if row:
            applied = {r[0] for r in self.db.execute("SELECT version FROM schema_migrations")}
        if SCHEMA_VERSION not in applied:
            with open(_MIGRATION, "r", encoding="utf-8") as f:
                self.db.executescript(f.read())
            self.db.execute(
                "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES (?,?,?)",
                (SCHEMA_VERSION, os.path.basename(_MIGRATION), _now()),
            )
            self.db.commit()

    def close(self):
        self.db.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.db.commit()
        self.db.close()
        return False

    # -- subjects -----------------------------------------------------------
    def subject(self, kind: str, name: str, *, content_hash: str | None = None,
                source_path: str | None = None, seed: int | None = None,
                meta: dict | None = None) -> int:
        """Get-or-create a subject, keyed by (kind, name, content_hash).

        Re-observing the same audio returns the same id so measurements
        accumulate. If meta is supplied on an existing subject it is merged in.
        """
        existing = self.db.execute(
            "SELECT id, meta FROM subjects WHERE kind=? AND name=? AND "
            "COALESCE(content_hash,'')=COALESCE(?,'')",
            (kind, name, content_hash),
        ).fetchone()
        if existing:
            if meta:
                merged = json.loads(existing["meta"] or "{}")
                merged.update(meta)
                self.db.execute("UPDATE subjects SET meta=? WHERE id=?",
                                (json.dumps(merged), existing["id"]))
                self.db.commit()
            return existing["id"]
        cur = self.db.execute(
            "INSERT INTO subjects(kind, name, content_hash, source_path, seed, meta, created_at) "
            "VALUES (?,?,?,?,?,?,?)",
            (kind, name, content_hash, source_path, seed,
             json.dumps(meta or {}), _now()),
        )
        self.db.commit()
        return cur.lastrowid

    def subject_for_file(self, path: str, kind: str, **meta) -> int:
        """Subject for an on-disk audio file (hashes its bytes for identity)."""
        name = os.path.basename(path)
        seed = meta.pop("seed", None)
        return self.subject(kind, name, content_hash=sha1_of_file(path),
                            source_path=os.path.abspath(path), seed=seed,
                            meta=meta or None)

    def find_subject(self, *, kind: str | None = None, name: str | None = None,
                     content_hash: str | None = None):
        clauses, args = [], []
        if kind is not None:
            clauses.append("kind=?"); args.append(kind)
        if name is not None:
            clauses.append("name=?"); args.append(name)
        if content_hash is not None:
            clauses.append("content_hash=?"); args.append(content_hash)
        where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
        return self.db.execute(
            "SELECT * FROM subjects" + where + " ORDER BY id DESC", args
        ).fetchall()

    # -- observations -------------------------------------------------------
    def record(self, subject_id: int, metric: str, value: float | None = None, *,
               vector=None, unit: str | None = None, confidence: float | None = None,
               window: str | None = None, tool: str = "unknown",
               tool_version: str | None = None, method: str | None = None,
               derived_from=None) -> int:
        """Append one measurement. confidence defaults from the grounded model."""
        if confidence is None:
            confidence = confidence_for(metric, window)
        value_json = None
        if vector is not None:
            value_json = json.dumps(list(vector))
        cur = self.db.execute(
            "INSERT INTO observations(subject_id, metric, value, value_json, unit, "
            "confidence, window, tool, tool_version, method, derived_from, created_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (subject_id, metric, value, value_json, unit, confidence, window, tool,
             tool_version, method,
             json.dumps(list(derived_from)) if derived_from else None, _now()),
        )
        self.db.commit()
        return cur.lastrowid

    def record_many(self, subject_id: int, metrics: dict, *, tool: str,
                    window: str | None = None, tool_version: str | None = None,
                    units: dict | None = None) -> list[int]:
        """Record a dict of {metric: value-or-vector}. Vectors (list/tuple) go to
        value_json; scalars to value. Confidence is derived per metric."""
        units = units or {}
        ids = []
        for metric, val in metrics.items():
            if isinstance(val, (list, tuple)):
                ids.append(self.record(subject_id, metric, vector=val, unit=units.get(metric),
                                       window=window, tool=tool, tool_version=tool_version))
            else:
                ids.append(self.record(subject_id, metric, float(val), unit=units.get(metric),
                                       window=window, tool=tool, tool_version=tool_version))
        return ids

    def latest(self, subject_id: int, metric: str):
        """Most recent observation of `metric` for `subject_id`, or None."""
        return self.db.execute(
            "SELECT * FROM observations WHERE subject_id=? AND metric=? "
            "ORDER BY id DESC LIMIT 1", (subject_id, metric)
        ).fetchone()

    def history(self, subject_id: int, metric: str):
        return self.db.execute(
            "SELECT * FROM observations WHERE subject_id=? AND metric=? ORDER BY id",
            (subject_id, metric)
        ).fetchall()

    def observations(self, *, subject_id: int | None = None, metric: str | None = None,
                     since: str | None = None):
        clauses, args = [], []
        if subject_id is not None:
            clauses.append("subject_id=?"); args.append(subject_id)
        if metric is not None:
            clauses.append("metric=?"); args.append(metric)
        if since is not None:
            clauses.append("created_at>=?"); args.append(since)
        where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
        return self.db.execute(
            "SELECT * FROM observations" + where + " ORDER BY id", args
        ).fetchall()

    # -- hypotheses ---------------------------------------------------------
    def hypothesize(self, subject_id: int, aspect: str, statement, *,
                    confidence: float = 0.5, evidence=None, tool: str = "cortex",
                    refined_from: int | None = None, status: str = "open") -> int:
        stmt = statement if isinstance(statement, str) else json.dumps(statement)
        now = _now()
        cur = self.db.execute(
            "INSERT INTO hypotheses(subject_id, aspect, statement, confidence, status, "
            "evidence, refined_from, tool, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (subject_id, aspect, stmt, confidence, status,
             json.dumps(list(evidence)) if evidence else None, refined_from, tool, now, now),
        )
        self.db.commit()
        return cur.lastrowid

    def refine_hypothesis(self, hyp_id: int, *, statement=None, confidence=None,
                          status="supported", evidence=None, tool: str = "cortex") -> int:
        """Create a NEW hypothesis refined from `hyp_id` and mark the old one
        superseded — the trail of reasoning is preserved, never overwritten."""
        old = self.db.execute("SELECT * FROM hypotheses WHERE id=?", (hyp_id,)).fetchone()
        if not old:
            raise KeyError(f"no hypothesis {hyp_id}")
        new_stmt = statement if statement is not None else old["statement"]
        new_conf = confidence if confidence is not None else old["confidence"]
        new_id = self.hypothesize(
            old["subject_id"], old["aspect"], new_stmt,
            confidence=new_conf, evidence=evidence, tool=tool,
            refined_from=hyp_id, status=status,
        )
        self.db.execute("UPDATE hypotheses SET status='superseded', updated_at=? WHERE id=?",
                        (_now(), hyp_id))
        self.db.commit()
        return new_id

    def hypotheses(self, *, subject_id: int | None = None, aspect: str | None = None,
                   status: str | None = None):
        clauses, args = [], []
        if subject_id is not None:
            clauses.append("subject_id=?"); args.append(subject_id)
        if aspect is not None:
            clauses.append("aspect=?"); args.append(aspect)
        if status is not None:
            clauses.append("status=?"); args.append(status)
        where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
        return self.db.execute(
            "SELECT * FROM hypotheses" + where + " ORDER BY id", args
        ).fetchall()

    # -- reconstructions ----------------------------------------------------
    def record_reconstruction(self, target_id: int, *, result_id: int | None = None,
                              hypothesis_id: int | None = None, recipe=None,
                              distance: float | None = None, distance_metric: str | None = None,
                              success: bool = False, observation_ids=None,
                              tool: str = "reconstruction") -> int:
        recipe_s = recipe if (recipe is None or isinstance(recipe, str)) else json.dumps(recipe)
        cur = self.db.execute(
            "INSERT INTO reconstructions(target_id, result_id, hypothesis_id, recipe, distance, "
            "distance_metric, success, observation_ids, tool, created_at) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (target_id, result_id, hypothesis_id, recipe_s, distance, distance_metric,
             1 if success else 0,
             json.dumps(list(observation_ids)) if observation_ids else None, tool, _now()),
        )
        self.db.commit()
        return cur.lastrowid

    def best_reconstruction(self, target_id: int):
        """The closest (min-distance) reconstruction attempt for a target."""
        return self.db.execute(
            "SELECT * FROM reconstructions WHERE target_id=? AND distance IS NOT NULL "
            "ORDER BY distance ASC LIMIT 1", (target_id,)
        ).fetchone()

    def reconstructions(self, target_id: int | None = None):
        if target_id is None:
            return self.db.execute("SELECT * FROM reconstructions ORDER BY id").fetchall()
        return self.db.execute(
            "SELECT * FROM reconstructions WHERE target_id=? ORDER BY id", (target_id,)
        ).fetchall()

    # -- summary ------------------------------------------------------------
    def stats(self) -> dict:
        q = lambda s: self.db.execute(s).fetchone()[0]
        return {
            "path": self.path,
            "subjects": q("SELECT COUNT(*) FROM subjects"),
            "observations": q("SELECT COUNT(*) FROM observations"),
            "distinct_metrics": q("SELECT COUNT(DISTINCT metric) FROM observations"),
            "hypotheses": q("SELECT COUNT(*) FROM hypotheses"),
            "reconstructions": q("SELECT COUNT(*) FROM reconstructions"),
            "successful_reconstructions": q("SELECT COUNT(*) FROM reconstructions WHERE success=1"),
        }
