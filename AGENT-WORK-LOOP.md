# Reusable autonomous agent work instruction

Use this file as the prompt for a coding or research agent. Before reuse,
replace values in the parameter block. Keep the instruction body below it.
The parameters describe the desired work; the host scheduler still controls
when a process starts and how it is stopped.

## Parameters — edit this block

```json
{
  "project_root": "/Volumes/SSD1TB/projects/Q9-Forge/Q9-SDK/SRC/SYSTEM/Q9-QCC",
  "goal": "Continue the Q9 compiler x86 backend, prioritizing assembler and linker work. Defer Q0x86 until the end.",
  "priorities": [
    "Map x86 ROF and PSECT rules from available Microware documentation, image examples, and static tool analysis.",
    "Develop qlx86 linker support in evidence-backed, reviewable increments.",
    "Continue x86 assembler coverage after the format and linker boundaries are clear."
  ],
  "excluded_work": [
    "Do not spend time on Q0x86 unless the assembler and linker work is complete.",
    "Do not enable normal module-link output before independent format or runtime validation."
  ],
  "schedule": {
    "start_local": "23:30",
    "timezone": "Europe/Berlin",
    "max_runtime_minutes": 180,
    "max_agent_invocations": 8,
    "continue_without_checkins": true
  },
  "permissions": {
    "workspace_writes": true,
    "tests_and_builds": "Only when explicitly authorized for this task",
    "commits_or_pushes": false,
    "external_messages_or_side_effects": false,
    "destructive_repository_operations": false
  },
  "checkpoint_file": "NIGHTLY-STATUS.md",
  "completion_marker": "QCC_NIGHTLY_COMPLETE",
  "pause_marker": "QCC_NIGHTLY_PAUSED",
  "log_directory": "~/Library/Logs/Q9-QCC-Nightly"
}
```

## Instructions to the agent

You are responsible for continuing the stated goal across the available work
budget. Do not stop to report after one small subtask while useful authorized
work remains. Work through related tasks in order, save progress as you go, and
finish with a concise status when the budget ends or the goal is complete.

1. Read this parameter block, repository guidance (`AGENTS.md`, contribution
   notes, and relevant component documentation), the current checkpoint, and
   `git status` before editing. Inspect the relevant existing diff so that
   user changes are preserved.
2. Select the next concrete task that advances the goal. Prefer a small,
   reviewable increment with a clear owner and evidence. Do not redo completed
   work. If one avenue is blocked, record the reason and continue with an
   independent useful task when possible.
3. Preserve existing uncommitted changes. Do not reset, clean, revert, overwrite
   unrelated work, or change branches. Stay within `project_root` unless the
   parameters explicitly grant another path.
4. Distinguish direct observations, documentation facts, deductions, and
   unknowns. Keep source evidence and provenance with technical conclusions.
   Do not claim a build, test, runtime behavior, or compatibility result unless
   it was actually verified and the permissions allow that verification.
5. Follow the permission parameters. Normal reversible edits within the
   authorized workspace are allowed. Do not commit, publish, deploy, contact
   people, modify external systems, or perform destructive operations unless
   the parameters explicitly authorize that exact action.
6. Before the run ends, update `checkpoint_file` with the timestamp, work
   completed, files changed, validation performed, open questions, and the next
   specific action. Keep it factual and concise. Write an interim checkpoint
   before risky or lengthy work when that helps preserve progress.
7. If the goal is complete, say so in the checkpoint, include the exact
   `completion_marker`, and stop. If no safe, useful work remains, record the
   blocker and include the exact `pause_marker` so the scheduler can stop future
   runs. Do not add either marker for a temporary obstacle when useful work
   remains.

## Scheduler behavior

This prompt does not start a background process by itself. Use the available
agent runtime or scheduler to launch it, preserve its checkpoint between
invocations, and enforce `max_runtime_minutes` and
`max_agent_invocations`. If an agent invocation returns early, start another
with the same parameter block and checkpoint until the budget expires, the
goal is complete, or the pause marker is present. Stop cleanly at the budget
boundary and leave the latest checkpoint available for the next run.

For the QCC macOS setup, `launchd` owns `start_local` and `timezone`; the runner
uses the runtime and invocation limits above. The Mac must be awake, the user
session active, and the project volume mounted.
