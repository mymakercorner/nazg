# Contributing to Nazg

Thanks for considering it. Issues, bug reports and pull requests are all welcome.

## Sign your commits (DCO)

Nazg uses the [Developer Certificate of Origin](https://developercertificate.org/) instead of a
contributor licence agreement. There is nothing to sign and no paperwork — you keep the
copyright on your own work.

Every commit needs a sign-off line:

```
Signed-off-by: Your Name <your.email@example.com>
```

Add it automatically with:

```
git commit -s
```

Or set it as the default for every repository:

```
git config --global format.signOff true
```

By signing off you are stating that you wrote the contribution, or that you have the right to
submit it under the project's licence. Use a name you are known by and a real email address.

## Licence

Nazg is GPL-3.0-or-later. Contributions are accepted under the same terms. New source files
should carry:

```
// SPDX-License-Identifier: GPL-3.0-or-later
```

## Before you write code

The project is in early development and the architecture is deliberate rather than accidental —
there is a written survey of the configuration protocols behind the design decisions. Please
open an issue to discuss anything substantial before building it, so the effort lands somewhere
useful.

## Protocol implementations

Protocols are re-implemented from their wire formats and public documentation. **Please do not
port code from other configurators** (vial-gui, the VIA app) into Nazg. Clean implementations
keep the project's licensing simple and its options open.
