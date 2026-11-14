# Contributing to AlacreOS

Use the [roadmap](docs/project.md) to find open decisions. Open a proposal in GitHub Issues with the relevant section of the [architecture specification](docs/architecture-v0.1.md), your assumptions, and the evidence you would use to compare alternatives.

For a focused fix, open a pull request in English and describe what changed and how you verified it. If a change affects a timing, isolation, or resource guarantee, identify the affected contract and what evidence needs updating.

Run `python3 -m unittest discover -s examples/edf -v` for changes to the reference analysis. Include the target configuration and reproduction commands when contributing [hardware qualification evidence](docs/qualification.md).
