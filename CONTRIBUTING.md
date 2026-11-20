# Contributing

Own code is GPL-3.0-or-later. Preserve upstream licenses and attribution for
third-party code and resources introduced later.

Working prototype code can be reused after reviewing its API, dependencies,
system effects, licensing and tests. Move it to the product that owns the
behavior. Adapt or rewrite it when needed to remove monorepository assumptions,
private configuration, personal paths or inappropriate product dependencies.
Preserve supported behavior and regression coverage.

Use Qt 6 and readable C++17 for desktop components. Builder's initial host CLI
uses Python 3.12+ and the standard library. Add abstractions when a concrete
caller needs them. Use isolated builds/GUI checks and native packaging tools.
KaryoForge source is under a separate agent's ownership.

Commits use an English subject, two substantive English paragraphs explaining
the problem, resulting behavior and validation, and an accurate co-author
trailer. Keep each change reviewable and complete.
