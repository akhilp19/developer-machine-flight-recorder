# DMFR Phase 0 — User Interview Guide

**Goal:** Validate that developers spend meaningful time debugging environment, build, and tooling regressions, and that a "flight recorder" for their machine would reduce that time.

**Target participants:** 10–15 developers across:
- Junior, mid-level, senior, staff engineers
- Multiple tech stacks (web, mobile, systems, ML/data)
- Team sizes: solo, small (2–10), medium (10–50), large (50+)
- Both in-office and remote workers

**Recruitment criteria:**
- Writes code at least 4 hours per week.
- Has experienced a "works on my machine" or sudden build-failure problem in the last 3 months.
- Willing to screen-share or describe a recent incident in detail.

---

## Interview Structure (45 minutes)

### 1. Warm-up (5 min)
1. Tell me about your current role and tech stack.
2. How long have you been on this team/project?

### 2. Recent incident deep-dive (15 min)
Think of the last time something worked yesterday but broke today, or a problem that took a long time to debug because of your local environment.

1. What happened? Walk me through it step by step.
2. What did you suspect first?
3. What did you actually check? (Git log, env vars, installed versions, CI, etc.)
4. What was the actual root cause?
5. How long did it take to find?
6. What would have helped you find it faster?

### 3. Tooling and workflow audit (10 min)
1. What version managers do you use? (nvm, mise, asdf, pyenv, etc.)
2. Do you use containers or devcontainers locally?
3. How do you keep your machine in sync with teammates?
4. What do you do when a teammate says "it works for me"?
5. Have you ever taken a screenshot of `env` or `which node` to debug a problem?

### 4. Concept reaction (10 min)
Describe the idea:
> "A background tool that records Git activity, system state, build output, and environment changes on your machine, so you can ask 'what changed in the last 4 hours?' and get a timeline of likely causes."

1. Is this something you would try?
2. Where would you expect to see the results? (CLI, IDE panel, web dashboard)
3. What would make you uninstall it?
4. What data would you be uncomfortable recording?
5. How much CPU/memory is acceptable for it to use?

### 5. Prioritization (5 min)
Rank these problems by how much time they cost you personally:
- Sudden local build failures
- Tests passing locally but failing in CI
- Environment drift between teammates
- Slow onboarding of new hires
- Debugging flaky tests
- Security/audit questions about what was installed

---

## Synthesis Template

For each interview, fill out:

| Field | Notes |
|-------|-------|
| Role / stack / team size | |
| Incident summary | |
| Time lost | |
| Root cause category | env / dependency / config / system / other |
| Current workaround | |
| Reaction to concept | |
| Biggest concern | privacy / performance / usefulness |
| Top requested feature | |

After all interviews, tally:
- Number of incidents per category.
- Median time lost.
- Most common reaction words.
- Most common concerns.
