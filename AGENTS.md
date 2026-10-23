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
- **Editing the fixture schema without re-recording `connect`.** A change to `protocols/stow/schema.yaml` can move the Describe chunk boundaries of the `connect` exchange.
- **Hiding a resource from the checker.** Apply the resource checker annotation to any resources that must be acquired and released. Carry them in a form the resource checker can validate or provide clear doc comments.

## Build, flash, test

Run inside `nix develop` (or `just docker-run`), on Linux or WSL.

Review the `justfile` for available commands.

## Verifying

Run `just format`, `just analyze` and `just lint` rather than invoking the tools by hand. Run the tests relevant to what you changed, and only those tests.

## Plans and work artifacts

Never commit spec, plans, or scratch files. These should be placed in the .scratch/ folder which is gitignored.

## Taste

- Comments describe how something is used, not what its lines do. A comment says what the code can't: a reason, or a workaround with its cause. Comments are concise, not prose, and code should mostly describe itself. Never leave a train of thought or a note about what changed; write every change as if it were the first implementation.
- Try to define a single source of truth for code and documentation.
- An acquired resource is released in the same function, or passed to a function annotated `@releases`. `PASS_OWNERSHIP` marks an exception. It flags that the reviewer must take care to understand the ownership model and lifecycle.
- When possible, Modules read from and publish to the Stow rather than exposing a direct function API to each other.
