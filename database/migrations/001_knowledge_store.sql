-- 001_knowledge_store.sql
--
-- THE RTG KNOWLEDGE STORE — the system's long-term memory.
--
-- A durable, append-only ledger of everything RTG observes, hypothesizes, and
-- reconstructs. The product directive is literal: "Never throw information
-- away. Store every measurement. Estimate confidence. Allow future analyzers to
-- consume previous measurements. Never allow knowledge to disappear." This
-- schema is that store.
--
-- Both the Python perception tools (tools/earview, tools/knowledge) and, in
-- time, the C++ engine (database/) speak this exact schema, so an observation
-- made by a vision analyzer and one made by the render loop live side by side
-- and can be compared. See docs/architecture/12-knowledge-store.md.
--
-- Determinism note (CLAUDE.md Rule 7): this store is METADATA that lives
-- entirely OUTSIDE the audio render path. It carries wall-clock timestamps and
-- monotonic row ids; nothing here ever feeds a seeded RNG or a sample buffer.
-- Recording an observation can never change rendered audio.

PRAGMA user_version = 1;

-- Bookkeeping so any future reader knows exactly what shape the store is in and
-- which migrations have run. Migrations are applied in filename order and are
-- idempotent (CREATE ... IF NOT EXISTS).
CREATE TABLE IF NOT EXISTS schema_migrations (
  version    INTEGER PRIMARY KEY,
  name       TEXT    NOT NULL,
  applied_at TEXT    NOT NULL
);

-- SUBJECTS ------------------------------------------------------------------
-- Anything observable: a reference track, one of our renders, an isolated stem,
-- a library sound, or a bounced mix/master. Identity is (kind, name,
-- content_hash) so re-observing the same audio ACCUMULATES observations against
-- one subject instead of forking a new one. A NULL content_hash still keys by
-- (kind, name) — useful before a file exists on disk.
CREATE TABLE IF NOT EXISTS subjects (
  id           INTEGER PRIMARY KEY,
  kind         TEXT    NOT NULL,               -- reference|render|stem|sound|mix|master
  name         TEXT    NOT NULL,
  content_hash TEXT,                            -- sha1 of the audio bytes when available
  source_path  TEXT,
  seed         INTEGER,                         -- generation seed (renders — reproducibility)
  meta         TEXT    NOT NULL DEFAULT '{}',   -- JSON: genre, bpm, params, lineage, notes
  created_at   TEXT    NOT NULL,
  UNIQUE(kind, name, content_hash)
);

-- OBSERVATIONS --------------------------------------------------------------
-- The append-only measurement ledger. Every number any analyzer EVER produces
-- lands here — never updated, never deleted. Each row carries:
--   * confidence  — how much THIS measurement should be trusted (0..1). The
--                   honest limits in docs/EARS_AND_EYES.md (degenerate/confounded
--                   metrics) become data here instead of prose.
--   * provenance  — which tool/version/method produced it.
--   * derived_from — the observation ids this one consumed, so a later analyzer
--                    can walk the lineage of any derived number.
-- Scalars go in `value`; vector metrics (bark[24], mfcc[13]) go in `value_json`.
CREATE TABLE IF NOT EXISTS observations (
  id           INTEGER PRIMARY KEY,
  subject_id   INTEGER NOT NULL REFERENCES subjects(id),
  metric       TEXT    NOT NULL,               -- namespaced, e.g. 'loudness.integrated_lufs'
  value        REAL,                            -- scalar value (NULL for vector metrics)
  value_json   TEXT,                            -- JSON array/object for vector metrics
  unit         TEXT,                            -- 'LUFS','acum','dB','ratio','hz',...
  confidence   REAL    NOT NULL DEFAULT 0.5,    -- 0..1 trust in this specific number
  window       TEXT,                            -- 'drop','break','full','stem:growl',...
  tool         TEXT    NOT NULL,                -- 'ears.py','engine','instruments.py'
  tool_version TEXT,
  method       TEXT,                            -- short note on HOW it was measured
  derived_from TEXT,                            -- JSON array of consumed observation ids
  created_at   TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_obs_subject_metric ON observations(subject_id, metric);
CREATE INDEX IF NOT EXISTS idx_obs_metric         ON observations(metric);

-- HYPOTHESES ----------------------------------------------------------------
-- The Producer Cortex layer: inferred production decisions, each a CLAIM with a
-- confidence and a lifecycle status, not a guess. "This growl is FM with a fast
-- formant sweep." Reconstruction supports or refutes them over time; a refined
-- claim links back to the one it replaces (refined_from) so the reasoning trail
-- is never lost.
CREATE TABLE IF NOT EXISTS hypotheses (
  id           INTEGER PRIMARY KEY,
  subject_id   INTEGER NOT NULL REFERENCES subjects(id),
  aspect       TEXT    NOT NULL,               -- 'growl.synth_method','drums.kick.tuning'
  statement    TEXT    NOT NULL,               -- JSON or text: the claim itself
  confidence   REAL    NOT NULL DEFAULT 0.5,
  status       TEXT    NOT NULL DEFAULT 'open',-- open|supported|refuted|superseded
  evidence     TEXT,                            -- JSON array of observation ids
  refined_from INTEGER REFERENCES hypotheses(id),
  tool         TEXT    NOT NULL,
  created_at   TEXT    NOT NULL,
  updated_at   TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_hyp_subject_aspect ON hypotheses(subject_id, aspect);
CREATE INDEX IF NOT EXISTS idx_hyp_status         ON hypotheses(status);

-- RECONSTRUCTIONS -----------------------------------------------------------
-- The Reconstruction Engine's log: an attempt to recreate a TARGET subject with
-- a recipe, and how perceptually close the RESULT got. Successes AND failures
-- are kept — a failure is diagnostic data (Self-Improvement Directive), never
-- discarded. Winning recipes are found by querying min(distance) per target.
CREATE TABLE IF NOT EXISTS reconstructions (
  id              INTEGER PRIMARY KEY,
  target_id       INTEGER NOT NULL REFERENCES subjects(id),
  result_id       INTEGER REFERENCES subjects(id),   -- the render we produced (a subject too)
  hypothesis_id   INTEGER REFERENCES hypotheses(id),
  recipe          TEXT,                              -- JSON synth genome / recipe reference
  distance        REAL,                              -- perceptual distance to the target
  distance_metric TEXT,                              -- 'ears.perceptual','bark.rmse',...
  success         INTEGER NOT NULL DEFAULT 0,        -- 1 if within the acceptance threshold
  observation_ids TEXT,                              -- JSON array of supporting observations
  tool            TEXT    NOT NULL,
  created_at      TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_recon_target ON reconstructions(target_id);
