# OS-9 process execution

## `system()`

Microware documents `system(const char *)` as a shell-command interface. The
command string is passed to the command processor named by `SHELL`; when that
variable is absent, the default OS-9 shell is used. The calling process waits
for the shell to finish, and `system()` returns the shell exit status.

This is therefore a blocking shell invocation, not a POSIX-style memory clone.
The command processor performs the argument parsing, path lookup and output
redirection. A raw command string must consequently be treated as shell
syntax, not as an already separated argument vector.

## `os9exec()`

Microware recommends `os9exec()` for programs that execute system commands.
It accepts an explicit argument vector, an environment vector, stack and
priority values, and an optional count of inherited paths. Its process
function is normally `os9forkc()` for a child process or `chainc()` when the
caller is intentionally replaced.

The important distinction for QCC is:

```text
system(command string)       shell parsing and shell context
os9exec(os9forkc, ..., argv) explicit arguments and inherited paths
```

The QCC driver should keep `system()` available as a normal C library
function, but use an internal argv-based execution wrapper for its pipeline.
That wrapper must also wait for the child and return its exit status.

## Consequence for the driver

The current `q9_system()` abstraction is sufficient for the host prototype,
but it is not the final OS-9 implementation. The next driver work package is
to replace its OS-9 branch with `os9exec()`/`os9forkc()` and explicit argument
arrays. This will remove ambiguities around working directories, quoting,
redirection and temporary-file paths.

Reference: Microware *C Library Reference*, sections `system()` (5-192) and
`os9exec()` (5-153), available at:
https://www.icdia.co.uk/microware/77165104.pdf
