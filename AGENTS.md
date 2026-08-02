# Repository Instructions

## Project direction

- ARGUS Redline is an off-grid, low-bandwidth coordination platform.
- Preserve the current single-Hub/single-Node architecture while developing the device shell, configuration model, command handling, diagnostics, and validation.
- Do not implement multi-Node, repeater, routing, or mesh behavior unless explicitly requested.
- Clearly distinguish current functionality, v1 targets, and post-v1 concepts in code, documentation, plans, and summaries.

## Protocol compatibility

- Preserve the Protocol v0.1 wire format unless explicitly instructed otherwise.
- Do not silently change packet layouts, field meanings, opcode values, status values, addressing behavior, or transaction semantics.
- Before proposing a protocol change, document its compatibility consequences for existing Hub firmware, Node firmware, deployed devices, and stored or captured packets.
- Treat packet types supported by the codec separately from firmware behavior that is actually implemented and exercised.

## Engineering rules

- Inspect relevant code and documentation before editing.
- Present a concise implementation plan before making changes.
- Keep changes small, scoped, and reviewable.
- Avoid unrelated formatting, cleanup, or refactoring.
- Avoid unnecessary dependencies.
- Do not commit, push, merge, tag, publish releases, or modify remote branches unless explicitly instructed.
- Never include credentials, private keys, device secrets, deployment secrets, or production data.
- Do not modify fixed hardware assumptions without identifying the affected board and the required test coverage.

## Validation

For firmware changes:

- Run the repository’s available PlatformIO build command.
- Prefer `platformio run`.
- Use `pio run` only if the `pio` executable is available.
- If neither command works, report that firmware validation could not be run.
- Do not install PlatformIO, create aliases, or modify PATH without explicit permission.
- Run `git diff --check`.
- Run relevant tests or static checks available in the repository.

After editing:

- Summarize changed files and behavior.
- Report commands run and their results.
- Identify anything not verified on physical hardware.
- Identify protocol, timing, persistence, or compatibility risks.
- Provide focused hardware-validation steps where applicable.

## Documentation precision

Use precise language for current behavior:

- Describe duplicate detection as single-entry and in-memory unless the implementation changes.
- Describe ACK behavior as regenerated from cached transaction metadata and status unless encoded-packet caching is implemented.
- Distinguish implemented packet types from packet types merely supported by the codec.
- Do not describe roadmap capabilities as currently operational.
