# Network Isolation

The network isolation of appbox is not implemented yet. This document is the
home of its architecture and records the packer side which exists today.

## Packer side

The `Network` workspace of `AppBox.exe` collects the network configuration of
the packaged application. Its tab strip carries the pages `Proxy`, `DNS` and
`IP Restrictions`; only the `DNS` page is implemented, the other two show the
empty state of a reserved isolation domain.

### DNS redirections

The `DNS` page holds the DNS redirections of the packaged application: every
row pairs a `Hostname or IP Address` with the `Redirect` address the name has
to resolve to inside the sandbox. The rows are edited inside the table:

- `Add...` appends a row and opens its hostname cell. The table keeps at most
  one row which is still being filled in, and the row reaches the model once
  both of its cells carry a value.
- `Remove` drops the selected row, including a row which was not completed.
- A hostname has to be unique (the comparison ignores the case, because the
  name resolution of the host does as well) and neither field may be empty or
  contain a whitespace character. A value the model refuses is reported and
  the stored value is put back into the cell.

The rules live in `src/core/NetworkModel.*`, which holds no wxWidgets
dependency and is unit tested by `test/unit/Unit_NetworkModel.cpp`; the
workspace itself is `src/widget/NetworkPanel.*` with the tab strip in
`src/widget/NetworkTabBar.*`.

### Scope

The redirections live in the session only. They are neither part of a project
file (`src/core/ProjectDocument.*`) nor of a packed archive, and the sandbox
does not hook the name resolution yet, so a packaged application resolves
names the way the host does. The workspace therefore collects the
configuration without changing the behaviour of a packed application.
