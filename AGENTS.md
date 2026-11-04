# Gantry

Gantry is a Zephyr module for building connected embedded applications. The architecture is event-driven and data-centric. It makes applications composable, reusable, and trivial to extend.

A Gantry application is composed of independent Modules. Each module communicates with the rest of the system through Events or Stow items. State is shared between modules via the Stow.

Detailed descriptions of each component can be found in `docs/`.

## What makes it special

- Modules are independent, composable, and reusable.
- Values can be easily shared between modules and exposed to external clients via the Stow.
- Application items, permissions, and constraints can be arbitrarily declared in a schema, not reimplemented per feature.
- Reference-counted memory is paired with a static resource checker to catch leaks at build time.

## Design philosophy

Prioritize simple systems and composable architectures. Do not copy complexity just because it is present elsewhere. Understand each need and find the smallest but architecturally sound and idiomatic approach. Look for opportunities for simplifications and improvements. Try to honor intent and avoid scope creep.

Code should be intuitive and have low mental overhead. It should self describe such that comments are not needed. All implementation work should adhere to the design philosophy and comment guidelines.

The rest of this document is guidance for contributing to and using the repo. The design philosophy should supersede anything else in this document or guidance. It is the north star.

## Glossary

- **module** an independent subsystem, usually with its own thread, that communicates only through events
- **event** a universal reference-counted message that modules pass through queues
- **Stow** the central store of device information, state, and configuration
- **item** an entry in the Stow, with a type, storage, permissions, and constraints

## Ways to hurt yourself

- **Editing a generated file.** Instead, edit the source and regenerate.
- **Committing a derived file.** Anything a build can produce should not live in git history.
- **Changing wire bytes without the protocol folder.** A wire change starts in `protocols/stow/stow.cddl`. Otherwise, the vectors fall out-of-date.
- **Editing the fixture schema without re-recording `connect`.** A change to `protocols/stow/stow.yaml` can move the Describe chunk boundaries of the `connect` exchange. Re-record each failing step by pasting what `just protocol_spec diag protocols/stow HEX` writes for the hex the replay printed.
- **Testing a wire behaviour with hand-encoded bytes in C.** Add an exchange to `protocols/stow/vectors.yaml` instead, so the server, the codec and the Python client all check it.
- **Hand-writing a wire constant in C.** C constants are generated from `protocols/stow/stow.cddl`. Add a new constant there, and a new prefix to `protocols/stow/constants.yaml`.
- **Hiding a resource from the checker.** Apply the resource checker annotation to any resources that must be acquired and released. Carry them in a form the resource checker can validate or provide clear doc comments.

## Build, flash, test

Run inside `nix develop` (or `just docker-run`), on Linux or WSL.

`just` lists the recipes in groups: setup, build, check, test and docs. Each tool's and sample's own commands live in a justfile in its directory, run as `just <module> <recipe>` from the root or `just <recipe>` from that directory.

## Verifying

Run `just fix`, then `just check`, rather than invoking the tools by hand. Run the tests relevant to what you changed, and only those tests, through `just test-suite NAME` or a module's `test`.

## Plans and work artifacts

Never commit spec, plans, or scratch files. These should be placed in the .scratch/ folder which is gitignored.

## Taste

- Comments describe how something is used, not what its lines do. A comment says what the code can't: a reason, or a workaround with its cause. Comments are concise, not prose, and code should mostly describe itself. Never leave a train of thought or a note about what changed; write every change as if it were the first implementation.
- Names are words, not letters. Variables and parameters say what they hold (`reader`, `writer`, `entry`), never `r`, `w` or `e`. Loop counters (`i`) are the only single-letter exception.
- Try to define a single source of truth for code and documentation.
- An acquired resource is released in the same function, or passed to a function annotated `@releases`. `PASS_OWNERSHIP` marks an exception. It flags that the reviewer must take care to understand the ownership model and lifecycle.
- When possible, Modules read from and publish to the Stow rather than exposing a direct function API to each other.

## Documentation

Pages in `docs/` explain a concept and its common path. Doxygen documents the full library interface.

- Write each page or section for one reader's task. A user page says how to use a feature. Contributor workflow (tests, vectors, recipes, regeneration) goes in the owning tool's README, or in this file when no tool owns it.
- Name a file only when the reader must create, open or edit it.
- Link to the API reference rather than restating a contract, error code or ownership rule.
- Document the common path. A sentence that exists because a test exists is not user documentation.
- Describe the system as it is, with no change history, rationale for a change, or roadmap.
- A change to an existing feature adds sentences, not sections. If a doc diff is longer than the user-visible change, cut it.
