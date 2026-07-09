# database — Sample Database + Knowledge Store (Storage)

SQLite access layer + versioned migrations, HNSW vector index over audio
embeddings, and the content-addressed FLAC blob store. Zero-admin, fully local.
See docs/architecture/03-technology-stack.md.

## Migrations

`migrations/` holds the versioned, filename-ordered, idempotent SQL schema.

- `001_knowledge_store.sql` — **the RTG Knowledge Store**: the system's
  long-term memory (subjects, append-only observations with confidence +
  provenance, hypotheses, reconstructions). This is the durable home for
  "never throw information away." The Python perception tools speak it today via
  `tools/knowledge/` (stdlib sqlite3); the C++ engine adopts the same schema as
  self-measurement gets persisted. Full spec:
  docs/architecture/12-knowledge-store.md.
