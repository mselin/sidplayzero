# Handover protocol

How a session keeps its working state in the repository, so that a session that is compacted, resumed or
replaced by a fresh one can carry on from the files and not from the conversation.

## 2. docs/HANDOVER.md

Keep it under about 150 lines. Rewrite sections in place and never append a log; history belongs in git.

- `## Task`: the task, verbatim
- `## Plan`: numbered steps, each with a completion check and a status
- `## Established`: verified facts with file:line or the command that showed them; decisions and why
- `## Changes`: what changed, per repository, with branch and commit or PR
- `## Verification`: what was run, the result, and what is still unverified
- `## Dead ends`: what was tried and failed, so nobody retries it
- `## Next`: the single next action, concrete enough to start cold
- `## Subagent files`: path and a one-line status for each

## 3. When to update it

- After writing the plan, after each plan step, after each verification run, and before every push. Commit it with the change it describes.
- If your context starts with a conversation summary instead of the original task message, a compaction happened. Re-read `docs/HANDOVER-PROTOCOL.md` and `docs/HANDOVER.md` before acting. Where the summary and the file disagree, the file wins.

## 4. Subagents

- Send verbose work (test runs, log reading, wide searches) to subagents so the output stays out of the main context.
- Only the main session writes `docs/HANDOVER.md`. A subagent gets its own file, `docs/handover/<slug>.md`, when its work will take more than about 15 tool calls, runs in parallel with other subagents, or produces findings the plan depends on. Quick lookups just return their answer.
- A subagent starts with none of this conversation. Its spawn prompt must contain: the assignment in full, the files to read first, its handover path, and the instruction to write that file before returning and to reply with a three-line summary plus the path.
- Subagent file sections: Assignment (verbatim), Findings (with file:line evidence), Changes, Verification, Open questions. Rewrite in place at each milestone, so a stopped subagent can be resumed or replaced from the file.
- Read the file, not only the summary, before building on a subagent's result, then list it under "Subagent files".

## 5. Finishing

- Move durable decisions and rules into the design doc and roadmap. Delete `docs/handover/*.md` files once their content is folded in.
- Leave `docs/HANDOVER.md` describing the finished state and candidate next tasks, and include it in the PR.
- Final reply: PRs opened, check status, and anything unverified.
