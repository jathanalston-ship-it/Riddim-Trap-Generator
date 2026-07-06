# engine/decision — AI Decision Engine
The 'producer brain': maps the 12 user parameters + seed to a complete GenerationPlan via rulebooks, weighted stochastic sampling, and a constraint solver. Genre rulebooks live in `data/`. No LLM. See docs/architecture/10-decision-engine.md.
