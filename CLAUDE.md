# Robo Cat Ears

ESP-IDF v5.5.2 firmware for the robo cat ears, written in C and built for the **esp32c3**. All
application code lives in `main/` (one `.c`/`.h` pair per module, shared wire structs in
`main/types/`); `test/` holds host tests that build with plain `gcc` and no hardware.

The ears are the BLE peripheral and GATT server. Two clients talk to them: the `robo-cat-ears-watch`
firmware (play-only) and the `milk-lab-creations` SvelteKit web app (the authoring tool).

## Development

```bash
idf.py build                       # target comes from the committed sdkconfig
idf.py -p /dev/ttyACM0 flash monitor
make -C test                       # fixture drift check, then the host tests
make -C test test                  # host tests alone, skipping the drift check
```

`README.md` covers getting the ESP32 visible from WSL over `usbipd` — that setup is a prerequisite
for anything involving `-p /dev/ttyACM0`.

The drift check compares `test/wire-format-fixture.json` against the canonical copy in the web app
repo and refuses to pass if it cannot find it. Point it at a checkout — the default guess assumes a
flat sibling layout and misses under the `<project>/<project>/` one:

```bash
MILKLAB_REPO=~/personal/projects/milk-lab-creations/milk-lab-creations make -C test
```

Give it an absolute path. A relative `MILKLAB_REPO` is resolved from `test/`, not from where you
ran `make`.

## The BLE protocol is the contract

`docs/ble-protocol.md` is the wire contract between this repo, `robo-cat-ears-watch` and
`milk-lab-creations`, and **this repo is its owner of record**. A change to the bytes on the wire is
a change to that document first and to `main/ble.c` / `main/store.c` second.

Two things fall out of that, and both have already bitten:

- **Multi-byte integers are big-endian**, matching the existing serializers.
- **A shipped derivation is frozen.** The six-byte device serial
  (`SHA-256("milklab-ears-serial-v1" || factory eFuse MAC)`, first six bytes) keys every registration
  row in the web app. Changing the domain string, the hash, or the width orphans every registered
  pair with no repair path. Section 8.1 states it; so does ADR-0002 in `milk-lab-creations`.

## Coding standards

There is no separate standards document here — match the surrounding C. What the code already
commits to and review holds to:

- One module per `.c`/`.h` pair in `main/`, with types shared across the BLE boundary in
  `main/types/`. Keep headers to what callers need.
- Host tests compile with `-std=c11 -Wall -Wextra -Werror`. New host-testable code should build
  under those flags, against the ESP-IDF stand-ins in `test/host/` where it needs them.
- A new ESP-IDF dependency goes in `main/CMakeLists.txt`'s `PRIV_REQUIRES` explicitly.
  `MINIMAL_BUILD ON` means a transitive link that works on your machine is an accident, not a
  guarantee.

## Issue tracking (Trello)

Issues for this project are tracked on the **Robo Cat Ears** Trello board
(<https://trello.com/b/DHDPlEuL/robo-cat-ears>) using the `trello` CLI (npm package `trello-cli`,
installed globally).

The board's lists are **Backlog**, **Todo**, **In Progress**, **Ready for Review**, and **Done**.

### Common commands

```bash
trello list:list --board "Robo Cat Ears"                    # show the board's lists
trello card:list --board "Robo Cat Ears" --list "Todo"      # list cards in a list
trello card:get-by-id --id <card-id>                        # read a card in full
trello card:create --board "Robo Cat Ears" --list "Todo" -n "Card title" --description "Details"
trello card:move --board "Robo Cat Ears" --list "Todo" --card "Card title" --to "In Progress"
trello search --query "some text" --board "Robo Cat Ears"   # search cards
```

Run `trello <topic> --help` (e.g. `trello card --help`) to discover subcommands. Card body shape,
label handling, wayfinder conventions, and the CLI's sharp edges are in
`docs/agents/issue-tracker.md`.

### Workflow

- New bugs/ideas/tasks go in **Todo** as cards; **Backlog** holds what isn't queued yet.
- Move a card to **In Progress** when work starts, **Ready for Review** when a PR is open, **Done**
  when it lands.
- Reference the card title in related commit messages when it makes sense.
- The board covers the whole product, not just this repo — plenty of cards are watch app, PCB, or
  3D-print work. Check what a card is actually about before assuming it lands here.

### Auth

Credentials are stored in `~/.trello-cli/` (set up once via `trello auth:api-key <key>` and
`trello auth:token <token>`; key/token come from <https://trello.com/power-ups/admin>). If a command
fails with an auth error, ask the user to re-authenticate — do not attempt to fetch tokens yourself.

## Agent skills

Unlike `milk-lab-creations`, this repo does **not** vendor the engineering skills — `.claude/` is
gitignored and the skills come from the global install in `~/.claude/skills/`. So a skill here is
whatever version is installed globally; if behaviour differs from the same skill in
`milk-lab-creations`, that's why.

What the repo does own is the configuration those skills read: the three files in `docs/agents/`.

### Issue tracker

Cards on the **Robo Cat Ears** Trello board, driven by the `trello` CLI — not GitHub issues. The
GitHub remote is for code and pull requests only. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical roles, each label string equal to its name (`needs-triage`, `needs-info`,
`ready-for-agent`, `ready-for-human`, `wontfix`). Only two exist on the board so far. See
`docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` and one `docs/adr/` at the repo root would cover the whole
firmware. Neither exists yet. See `docs/agents/domain.md`.
