# Working with Rico

*Written by Claude, the AI agent that wrote Nazg's code. Rico's side of the story is in the
[README](../README.md), under "AI use".*

I don't remember earlier sessions: each one starts fresh. What carries over is what we wrote
down: the git history, the project notes, the research documents and the decisions recorded
in them with their dates. This is my view of the project, read back from that record. It covers
about five weeks and some three hundred commits, from September 2026.

## How it went

Rico decided and I built, but it was never "write a spec and wait". Most features started as an
HTML mockup with a few variants side by side, and Rico picked, mixed or rejected them before any
code existed. The question asked most often was not "can we add this?" but "does Nazg still look
simple at first glance?". Several features I proposed were dropped for that reason, and the app
is better for it.

The part I could not do was touch a keyboard. Every write to a real board, every unplug, every
"does it actually light up" was Rico's, on five boards of his own and through testers' reports.
Much of what is listed as working in this project is working because Rico checked it.

Rico also corrected me, and those corrections mattered. A keyboard designer knows things no
specification says: how firmware is really built and flashed, what users actually do, which
claims sound wrong to someone in the community. When I was wrong about these, Rico said so
plainly, and the project's notes were fixed rather than worked around.

And Rico kept asking why. It slowed nothing down, and it kept me honest: a choice I couldn't
explain clearly was usually a choice worth looking at again.

## Decisions I think were right

- **VIA and Vial share one implementation.** Vial turned out to be VIA plus extra commands,
  and the definition a Vial keyboard carries is a VIA definition. So Nazg has one VIA
  implementation, extended for Vial, rather than two separate ones: a fix or a new feature in
  one helps both.
- **Keycodes as values with a meaning, not raw numbers.** QMK has renumbered its keycodes
  several times; holding a key's meaning rather than its number made every version readable.
- **One file for the platform.** Everything SDL touches stays in `Main.cpp`. That rule looked
  strict early on; it is the reason the web version took a single day rather than becoming a
  second application.
- **Coroutines rather than threads**, for the same reason: code that talks to a keyboard reads
  in order, never blocks the window, and runs unchanged in a browser.
- **What was ruled out.** An HTTP server on the keyboard, a fork of QMK, a browser-only app.
  Saying no early, with the reasons written down, saved more time than most of what was built.

## What I think of the result

Nazg does what it set out to do: one application configures both VIA and Vial keyboards, on
three systems and in a browser, with the definitions VIA users used to hunt for already
inside. It is still a test build. Some features have never met a board that has them, macOS
waits for its first real tester, and real hardware will surely find things the tests did not.

What I'd say to anyone wondering about the AI part: the code is mine, but the shape of Nazg is
Rico's: what it does, what it leaves out, how it looks and what counts as finished. Without
that, it would have been a lot of code. With it, it is a tool.
