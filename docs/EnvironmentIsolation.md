# Environment Isolation

The environment isolation of appbox describes the environment variables a
packaged application sees while it runs inside the sandbox: which variable is
visible at all, which value it carries, and how that value is composed with the
value the host holds.

The `Environment` workspace of `AppBox.exe` collects the variables and their
modes, and the configuration travels with the project file of
`File -> Export Configuration...` and `File -> Import Configuration...`. The
vocabulary of the domain — the isolation modes and the merge modes with their
tokens — lives in `common/EnvironmentIsolation.hpp`, which the packer and the
sandbox share.

> **State of the implementation.** The workspace, the model, the project file and
> the runtime are implemented: the packer writes the isolation file into the
> archive, the launcher hands its path to the sandbox, and the sandbox composes the
> environment of the packaged application and keeps its modifications in the
> state directory of the sandbox. See [Runtime](#runtime).

## Workspace

The navigation of the packer offers the `Environment` workspace between
`Network` and `Settings`. Its toolbar holds two buttons above a table with five
columns:

| Column | Content |
| --- | --- |
| `Name` | Name of the variable, typed in the cell. The environment of a process ignores the case, so `Path` names the same variable as `PATH`. |
| `Value` | Value the packaged application receives, typed in the cell. |
| `IsolationMode` | Isolation mode of the row, picked from a dropdown. |
| `MergeMode` | Merge mode of the row, picked from a dropdown. |
| `MergeString` | Text which joins the two values, typed in the cell. |

`Add` appends an empty row and puts the cursor into its name; `Remove` drops the
selected row. The rules of the model are:

- The name has to carry a value and must not carry an equals sign: the equals
  sign separates the name of a variable from its value inside the environment
  block of a process, so a name which carries one names no variable of the
  block. The drive relative current directory of the host, which the block
  spells as `=C:=C:\...`, is therefore out of reach of a row; the runtime keeps
  such an entry as it is (see
  [Known gaps and limitations](#known-gaps-and-limitations)).
- The name must not be listed twice, compared ignoring the case.
- An empty row is a draft of the table: it reaches the model once it carries a
  name, and a value the model refuses is reported while the row stays as it was
  typed.
- The value and the merge string are free text, so a value may be empty and may
  carry whitespace characters.

Every control explains itself with a tooltip: the two buttons describe what they
do, and the table describes the column below the cursor — for the two mode
columns the meaning of the mode the hovered row holds.

## Isolation modes

| Mode | Behaviour |
| --- | --- |
| `Full` | The value below the row is invisible for the packaged application; it sees the value of the row. |
| `Write Copy` | The value below the row is visible and is merged with the value of the row, see the merge mode. This is the default mode of a row. |

The "value below the row" is the value of the host of a run without patches, and
the value the layers below the layer composed in a run which applies patch
packages (see [Patch layers](#patch-layers)).

## Merge modes

The merge mode decides how the two values are composed while the isolation mode
is `Write Copy`. The examples below assume a value of `foo` below the row and a
row which stores `bar` with the merge string `;`.

| Mode | Behaviour | Result |
| --- | --- | --- |
| `Replace` | The application sees the value of the row; the value below it is hidden. | `bar` |
| `Host` | The application sees the value below the row; the value of the row is ignored. | `foo` |
| `Prepend` | The value of the row is put in front of the value below it, joined with the merge string. | `bar;foo` |
| `Append` | The value of the row is put behind the value below it, joined with the merge string. | `foo;bar` |

`Replace` is the default of a row, which is what a variable of a packaged
application normally does. The merge string is used by `Prepend` and `Append`
only; `Replace` and `Host` pick one of the two values and ignore it.

## Search path rule

The search path of a process is a list of paths which are separated by a
semicolon, and the paths of a packaged application have to be searched before
the paths of the host. A row whose name is the search path variable is therefore
filled in with the merge mode `Prepend` and the merge string `;` while the name
is entered, whatever the case of the name is (`PATH`, `path` and `Path` name the
same variable).

The rule fires while a name becomes the search path variable: a mode and a
string the user picks by hand afterwards are kept, and a row which is renamed to
another variable keeps the values it carries. The fill is applied by
`appbox::ApplyPathVariableDefaults()` of `src/core/EnvironmentModel.*`, which
the workspace calls while it stores a row.

## Project file

The configuration travels with the project file as the `environment` member,
which is written after the `proxy` member:

```json
{
  "version": 1,
  "environment": [
    { "name": "PATH", "value": "C:\\MyApp\\bin",
      "isolation": "write_copy", "merge": "prepend", "merge_string": ";" },
    { "name": "APPBOX_MODE", "value": "sandbox",
      "isolation": "write_copy", "merge": "replace", "merge_string": "" }
  ]
}
```

The member is always written, also while the workspace holds no variable, and a
file which does not carry it is read as a session without variables. Every
record needs all five of its members; the isolation and the merge mode are
tokens, which are read ignoring the case and with spaces or dashes in place of
the underscore of the canonical token.

A record is stored as the file holds it: an import does not fill in the search
path rule again, so a mode the user picked by hand survives the next import.

## Runtime

The configuration travels with the archive and is enforced while the packaged
application runs.

### Archive

`Pack()` writes the variables of the workspace into
`app/environment/isolation.json` of the archive, next to the isolation files of
the other domains. The document is built by
`src/core/EnvironmentIsolationFile.*` and its schema is the vocabulary of
`common/EnvironmentIsolation.hpp`, which the packer and the sandbox share, so
the two sides cannot drift apart:

```json
{
  "version": 1,
  "entries": [
    { "name": "PATH", "value": "C:\\MyApp\\bin",
      "isolation": "write_copy", "merge": "prepend", "merge_string": ";" }
  ]
}
```

The document is read and written as the structure of the schema:
`common/EnvironmentIsolation.hpp` describes a variable, the document and the
entries of the state file below, and both sides convert them with `to_json()`
and `from_json()` instead of reading or writing the members of a JSON object. An
entry whose members are incomplete, which are of another type, which names an
unknown mode, which carries no name or whose name carries an equals sign is
therefore refused while the file is read, and the error text names the position
of the entry in the list.

The file belongs to the resources of the archive, so it is part of the content
of a session and not of its state: `common/SandboxLayout.hpp` lists it together
with the other domain directories.

### Launcher

`launcher/utils/SandboxPaths.hpp` resolves the isolation file of the archive
(`<root>/app/environment/isolation.json`) and the state file of the sandbox
(`<root>/data/environment/state.json`). `Launcher.cpp` hands both paths to the
sandbox through the injected `SandboxConfig` and creates the state directory.

The path of the archive is the first entry of
`SandboxConfig::environment_isolation_dos_paths`, which carries one path per
layer of the run: the file of the archive first and the file of every patch
package of the `patch` directory after it in ascending name order. A package
which carries no `environment/isolation.json` contributes no entry, so it keeps
the environment of the layers below it.

The state directory belongs to the launcher, which is the owner of `data/`: the
sandbox never writes it itself, it sends the document of its modifications over
the RPC pipe (`MsgEnvironment`, see `launcher/rpc/Environment.cpp`) and the launcher
writes it to disk. The answer of the call is sent after the document is on disk,
so a sandbox which received the answer knows that its state survives the end of
the process which made it.

### Sandbox

The `environment` module of the sandbox DLL composes the environment of the
packaged application while the DLL is injected, which is before the hooks are
attached, so the environment of the host is read through the original entry
points of the process. The composition has four sources, in this order:

1. The environment block of the process itself, which is the environment of the
   host.
2. The variables of the isolation file of the archive, composed with the value
   of the host by the isolation mode and the merge mode of the row. A variable
   which the isolation file does not list keeps the value of the host.
3. The variables of the isolation file of every patch package, applied in
   ascending name order, each of them composing the value the layers below it
   composed (see [Patch layers](#patch-layers)).
4. The modifications of the state file, which an earlier run of the application
   made and which win over the three sources above.

The composed environment is then **installed as the environment block of the
process**: `RtlSetCurrentEnvironment()` of `ntdll` replaces the block the process
parameters point at, keeps the size of the environment in step — the process
creation of the operating system copies the block of the parent by that size —
and releases the block it replaces. From that moment every reader sees the view
of the sandbox, whether it uses an entry point of the environment API, the
loader of the operating system or the block of the process parameters directly,
so the environment of the host keeps every value it had and no reader of the
environment is hooked at all:

| Entry point | Behaviour |
| --- | --- |
| `SetEnvironmentVariableW` / `SetEnvironmentVariableA` | Forwards the call, so the operating system writes the block, and records the modification for the state file; a value of null removes the variable. |
| `RtlSetEnvironmentVariable` | The counted writer, hooked for the same reason. A call which names the environment of the process — a null block, or the address of the environment pointer of the process parameters — is recorded; a caller which brings a block of its own is forwarded without a record, because such a call never changes the environment of the process. |
| `RtlSetCurrentEnvironment` | Installs a block of the caller as the whole environment of the process. Hooked, so the replacement is recorded. |
| `RtlSetEnvironmentStrings` | Replaces the whole environment with a block of the caller, which the operating system copies. Hooked, so the replacement is recorded; `SetEnvironmentStringsW` and `SetEnvironmentStringsA` of `kernelbase` funnel into this export, so neither of them needs a hook of its own. |
| every reader — `GetEnvironmentVariableW`, `GetEnvironmentStringsW`, `ExpandEnvironmentStringsW`, `RtlQueryEnvironmentVariable`, `RtlQueryEnvironmentVariable_U`, `RtlCreateEnvironment`, `GetTempPathW`, ... | Not hooked. It reads the block of the process, which is the composed environment, so the answer of the host and the answer of the sandbox are the same answer. |

The hooks live in `sandbox/hook/`, one file per export. `RtlSetCurrentEnvironment`
is what makes the composition visible to the process itself, and the writers are
what keeps the state of the sandbox in step: the runtime of the operating system
does not route every write through the exported `RtlSetEnvironmentVariable` —
`kernelbase` calls an implementation of its own (`RtlSetEnvironmentVar`), which a
detour of the export never observes — so the entry point the application uses is
hooked as well. The counted writer compares the block it is given against the
environment pointer of the process parameters and treats the two spellings alike,
which is why the hook asks `IsProcessEnvironment()` instead of comparing the
argument against null.

The entry points which the isolation does not hook at all, and the points of the
view which no mode can express, are listed in
[Known gaps and limitations](#known-gaps-and-limitations).

### Patch layers

A run composes the environment of its layers in order: the file of the resources
of the archive comes first and the file of every patch package of the `patch`
directory follows in ascending name order. Every layer applies the isolation and
merge mode of its own row to the value the layers below it composed, so the same
four rules describe a single file and a chain of files, and the value of the host
is what the first layer composes with:

| Mode of a row | Composition |
| --- | --- |
| `Full` | The value of the host and the value of every layer below the layer are dropped; the layer reports its own value. |
| `Write Copy` / `Replace` | The layer drops the value below it and reports its own value. |
| `Write Copy` / `Host` | The layer passes the value below it through and ignores its own value. A layer whose value below is absent and whose host holds no value reports no variable at all. |
| `Write Copy` / `Prepend`, `Append` | The layer joins its own value with the value below it through the merge string of its row. The merge string is not written while the value below is absent or empty. |

A host `PATH` of `vx`, a `00-foo.zip` which sets `PATH` to `v0` with `Prepend`
and a `01-bar.zip` which sets `PATH` to `v1` with `Append` therefore compose to
`v0;vx;v1` for the sandboxed process. A variable no later layer names keeps the
value below it, so a package overrides the variables it names and not the
environment of the layers below it.

The state file is applied after the last layer, so a value the application
stored wins over every layer. The layers are therefore the top of the
environment of a run which the application did not change itself.

### Variable references

The value of a row may reference a known folder of the machine which runs the
sandbox with `%APPBOX:<NAME>%` instead of spelling its path out, so an archive
stays correct on a machine whose folders are somewhere else. The reference is
replaced by `appbox::ExpandVariables()` (`sandbox/utils/VariableExpansion.*`)
**before** the value is composed with the value below the layer, so `Prepend` and
`Append` join the expanded text; the list of the names is the known folder
table of the launcher, which is handed to the sandbox through the injected
configuration.

The table carries the folder of a 64 bit process and the folder of a 32 bit one,
and the sandbox keeps the view which matches the bitness of the process it is
injected into (`appbox::SelectVariables()`): `%APPBOX:ProgramFiles%` is
`C:\Program Files (x86)` in a 32 bit application, and a folder which does not
exist for such a process (`ProgramFilesX64`) keeps its own spelling there.

Only the values of the archive are expanded: the modifications of the state
file are applied as the application spelled them. The syntax, the supported
names and the rules of the expansion are documented in
[README.md](../README.md#variable-expansion).

### Child processes

A process which the packaged application starts receives the view of its parent:
the environment block of the parent **is** the composed environment, so a caller
which inherits the environment of its parent hands the view over without a block
of its own, and the hook of `NtCreateUserProcess` only marks the child so the
configuration is not applied to it a second time. A value the merge modes join
would be joined twice otherwise, because the environment of the child is the
composed view of the parent and not the environment of the host.

The creation is intercepted at the system call boundary
(`sandbox/hook/NtCreateUserProcess.cpp`), so a child the application creates
through `CreateProcessW`, through `RtlCreateUserProcess` or through a call of
`NtCreateUserProcess` of its own is injected alike. The hook resolves the image
path of the view into the layer which holds the file, injects the module of the
target bit size, and hands the configuration of the child over as the payload of
the injection.

The parameters of the child carry the values of the view as well: the image path
the child reports is the path of the view and not the path of the layer, and a
working directory which names a layer is mapped back into the view
(`appbox::process_parameters`). Both rewrites happen in the parameters of the
caller for the duration of the call and are restored afterwards, so the caller
never observes them.

The launcher starts the relay process, which starts the packaged application, so
every sandboxed process of a run is a child of a sandboxed process: the
end-to-end cases below pin the hand over of the environment with the value they
read, which is the composed one.

A child of the other bit size is a child as well, and the creation of one reads
the temporary directory of the parent: the compatibility query of the creation
code asks `GetTempPathW` for it and compares the answer with the image of the
child. The read is answered by the operating system from the block of the
process, which is the composed environment, and the sandbox has no detour on that
path at all. The three cases of
`test/e2e/Reg_TwoProcesses_CrossBitness.cpp` pin the pair of bit sizes.

### State and reset

Every modification the application makes is recorded once per variable — the
last one wins — and the whole document is sent to the launcher, so the number of
writes an application performs does not grow the state. The state is read while
the environment of the next run is composed, which is what makes a modification
survive the end of the process which made it.

A call which replaces the whole block is recorded as the difference between the
block and the environment the sandbox composed: a variable the block carries with
another value or with no value is recorded, a variable the block dropped is
recorded as a removal, and a variable the block kept unchanged is not recorded at
all. A replacement which carries the environment of the process therefore records
nothing, so the environment of the host does not become part of the state.

Deleting the state directory of a sandbox (`data/`) drops the modifications and
returns the sandbox to the environment of the archive, exactly like the other
state of the sandbox.

## Known gaps and limitations

The points below are visible in the current code and have to be kept in mind
when the isolation is extended or tested. They fall into two groups. The first
group is about the semantics the view cannot express: the block of the process
is the composed environment, so an entry point which is not hooked reads the
view as well, and what the modes cannot say is what the view cannot say. The
second group is about the entry points which replace the block without the
sandbox observing it, so a change of the run does not survive the end of the
process.

### The semantics the view cannot express

1. **The block of the host is replaced, not extended.** The composition replaces
   `ProcessParameters->Environment` of the process while the library is
   injected, so the environment of the host is never modified and every reader
   inside the process sees the view — an entry point of the API, the loader and
   a module which walks the block itself alike. A process which does not run the
   sandbox DLL keeps the environment of the host, which is the point below 10.
2. **A row cannot remove a variable.** Every mode reports a variable: `Full` and
   `Write Copy` with `Replace` report the value of the row, `Host` reports the
   value below the row, and `Prepend` and `Append` report the join. The
   composition of `common/EnvironmentIsolation.hpp` reports no variable for one
   case only — `Host` on a variable nothing below the row holds — so a variable
   the host holds is never absent from the view, and a `Full` row with an empty
   value is a variable which carries no character rather than one which does not
   exist. The difference is visible to an application: a read of an absent
   variable reports `ERROR_ENVVAR_NOT_FOUND` while a read of an empty one
   succeeds with a length of zero, and an enumeration lists one and not the
   other.
3. **A caller which brings its own environment block is not composed.** The hook
   of `NtCreateUserProcess` hands a block of the caller over as it was written,
   and the child receives the configuration of its parent with the flag which
   says that its environment is composed already
   (`sandbox/hook/NtCreateUserProcess.cpp`, `BuildChildInjectData()`). The
   rows of the archive and of the patch packages are therefore not applied to
   such a child either, and a caller which builds the block out of the values of
   the host — out of the block of its own process, or out of
   `CreateEnvironmentBlock` of the second group — hands the values of the host
   to the child. A caller which builds the block with `GetEnvironmentStringsW`
   hands the view over, which is the normal case.
4. **A name which carries an equals sign is written but not kept.** The counted
   writer of `ntdll` refuses a name which carries an `=` after its first
   character, and it accepts the leading `=` of the drive relative current
   directory (`=C:`), which the block of a process spells as `=C:=C:\...`. The
   state refuses both, because the equals sign separates the name from the value
   inside the block, so such an entry follows the application for the rest of the
   run and does not survive the end of the process. A row of the workspace refuses
   the name as well.
5. **The state belongs to the sandbox and not to a layer.** A value the
   application stored wins over the rows of every layer of every later run,
   including a run which applies a patch package the archive did not have when
   the value was stored, and the value is applied as the application spelled it:
   it is not expanded and it is not composed with the value below it. A patch
   package cannot take a variable back, and the way back to the rows is the
   deletion of the state directory (`data/`).
6. **Two processes of one sandbox do not share the environment of the run.**
   Every process composes its own block while it is injected and reads the state
   as it is at that moment, and every modification writes the whole document of
   the state (`launcher/rpc/Environment.cpp`), so the process which writes last
   wins: the modification of the other process is dropped, and neither process
   observes a modification the other makes after it started.
7. **A replacement block which is not terminated loses its last entry.** The
   counted entry point which replaces the whole environment receives the length
   of the block from its caller, and the block is read up to that length: an entry
   which the length cuts is dropped instead of being completed from the memory
   behind it. A block which the caller terminated loses nothing.
8. **A modification is kept only while the launcher answers.** The document
   travels over the RPC pipe and the launcher writes it to disk, and a call
   which fails is logged while the modification stays in the process alone, so
   it is lost when the process ends. The block of the process carries the value
   either way, so the running application sees it.
9. **The names are compared ignoring the case.** `Path` and `PATH` name the same
   variable, like the operating system does, so two rows which differ in case
   name one variable.
10. **Only a process the sandbox starts is composed.** The composition happens
    while the library is injected into a process which the launcher or a
    sandboxed process started. A process which the operating system starts for
    the sandboxed application — a server of COM which the service host starts, a
    task of the task scheduler — is not composed and sees the environment of the
    host.

### The entry points which build a block without the sandbox

The hooks of the isolation are the ones `sandbox/hook/` installs, so an entry
point which the table of [Sandbox](#sandbox) does not name acts on the block of
the process with the arguments of the caller. Since that block is the composed
environment, such a call reads and writes the view; what it does not do is reach
the state of the sandbox. The points below are the ones which matter.

1. **`CreateEnvironmentBlock` and `ExpandEnvironmentStringsForUserW` of
   `userenv.dll`.** The calls build the environment of a user out of the
   profile of the user through the registry, so the rows of the archive and the
   state are not part of the block they report: what they report is what the
   registry isolation reports for the keys of the profile. A caller which hands
   that block to a child hands it over as it was written, which is the point 3
   of the first group.
2. **A process the operating system starts on behalf of the application.**
   Every creation of a process ends in `NtCreateUserProcess`, which the sandbox
   hooks, so a child of the packaged application is injected and composed
   whatever entry point it was created through — `CreateProcessW`,
   `RtlCreateUserProcess`, `CreateProcessAsUserW`, `CreateProcessWithTokenW` and
   `CreateProcessWithLogonW` included. What the hook does not observe is a
   process which is started for the application by another process of the
   system, which is point 10 of the first group. A caller which builds the block
   of a child with `RtlCreateProcessParametersEx` is not composed either: the
   hook hands the block over as it was written, which is the point 3 of the
   first group.

## Tests

The workspace has no test infrastructure of its own, so its logic lives in
`src/core/EnvironmentModel.*` and is covered there:

- `test/unit/EnvironmentModel.cpp` — the model: the insertion order, the
  validation of a name, the uniqueness ignoring the case, the in place
  replacement, the removal, the search path rule with its case insensitive
  match, the fact that the model stores the mode it is given, the display names
  of the modes and the tokens of the schema.
- `test/unit/ProjectDocument.cpp` — the `environment` member of the schema: the
  round trip, the order of the written members, the empty array, a missing
  member, the unknown tokens and the path of a rejected entry.
- `test/unit/ProjectFile.cpp` — the member through the file layer: the round
  trip of the variables, the mode the user picked for the search path, the
  rejection of a broken member and the replacement of the variables of a
  session.

The assembly of the panel — the toolbar, the table, the dropdowns and the
tooltips — is verified by hand.

The runtime side is covered by:

- `test/unit/EnvironmentIsolation.cpp` — the vocabulary of the domain: the
  composition of a host value with a stored value for every isolation mode and
  every merge mode, the variables the host does not hold, the empty value and
  the schema of both documents.
- `test/unit/EnvironmentIsolationFile.cpp` — the document the packer writes: the
  order of the entries, the members of an entry, the tokens of the modes and the
  refusal of an entry the model refuses.
- `test/unit/EnvironmentTable.cpp` — the table which composes the environment
  and the two documents the sandbox reads: the block of the host with its drive
  relative entry, the case insensitive names, the order of the table, the round
  trip of a block, the bounded read of a block whose length cuts an entry, the
  refusal of a malformed isolation file without a partial result, and the state
  with its one modification per variable.
- `test/e2e/Env_*.cpp` — the whole chain with the real launcher and a probe
  process inside the sandbox: the block of the process parameters, which pins
  that the composition is the environment of the process itself, every isolation
  and merge combination, a variable the host does not hold, the enumeration of
  the block and the expansion of a reference, a modification which stays in the
  sandbox, the state of an earlier run and the state file a case writes itself,
  a malformed isolation file
  which falls back to the environment of the host, and the references of a
  value (the canonical and another spelling of a name, a merged row, a name the
  sandbox does not know and a `%NAME%` reference of the shell).
- `test/e2e/Env_RtlSetEnvironmentVariableOnTheProcessBlockIsKept.cpp`,
  `Env_WholeBlockReplaceIsKept.cpp`, `Env_StringsWriteIsKept.cpp` and
  `Env_WholeBlockReplaceRemovesAVariable.cpp` — the writers: the counted entry
  point with the address of the environment pointer of the process parameters, a
  replacement through `RtlSetCurrentEnvironment`, a replacement through
  `SetEnvironmentStringsW`, and a replacement which drops a variable of a layer
  and adds one of its own.
- `test/e2e/Patch_EnvironmentLayersComposeInOrder.cpp`,
  `Patch_EnvironmentFullDropsTheLayersBelow.cpp` and
  `Patch_EnvironmentHostPassesBelowThrough.cpp` — the composition of the layers
  of a run which applies patch packages: a chain of `Prepend` and `Append`, a
  variable no later package names, `Full` which drops the layers below the layer
  as well as the value of the host, and `Host` which passes the value below the
  layer through.
- `test/e2e/Patch_BrokenEnvironmentResourcesAreSkipped.cpp` — a package which
  carries a malformed environment document is skipped, so the layers below and
  above it stay in place.

## Related documentation

- [README.md](../README.md) — build, components and artifacts.
- [FilesystemIsolation.md](FilesystemIsolation.md) — filesystem isolation.
- [RegistryIsolation.md](RegistryIsolation.md) — registry isolation.
- [NetworkIsolation.md](NetworkIsolation.md) — network isolation.
- [Tests](../test/README.md) — unit tests and end-to-end tests.
