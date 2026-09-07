#!/usr/bin/env python3
"""agy lifecycle hooks — procedure, not instruction.

One script handles every event; the event name is argv[1]:

  pre-tool   gate run_command: hard-deny destructive commands, auto-allow the
             project's known-safe verification commands, ask for the rest.
  post-tool  record whether a verification command actually succeeded.
  pre-invoke on the first invocation, snapshot the working tree.
  stop       refuse to end a session that changed files without a successful
             verification run (at most once, so the loop cannot hang).

Per-project configuration lives in `.agents/agy.conf` next to this hook
(`KEY=value`, `#` comments). Recognised keys:

  PLAN_GATE   on|off — refuse the first edit until a plan has been recorded
  VERIFY_CMD  the command a human would name as "did it still work"
  VERIFY_RE   regex matching commands that count as verification
  ALLOW_RE    regex matching commands safe to auto-approve
  DENY_RE     extra regex of commands to hard-deny

Everything is best-effort: on any internal error the hook stays out of the way.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

# Commands that destroy work that is not ours to destroy. Draft RFC-AGY-001 §6.
DEFAULT_DENY = (
    r"git\s+reset\s+--hard"
    r"|git\s+clean\s+-[a-z]*f"
    r"|git\s+checkout\s+--\s"
    r"|git\s+restore\s+(?!--staged)"
    r"|git\s+push"
    r"|git\s+commit\s+--amend"
    r"|git\s+rebase"
    r"|git\s+filter-branch"
    r"|rm\s+-[a-z]*r[a-z]*f\s+/(?:\s|$)"
)

# Read-only inspection: approving these costs nothing and removes prompts.
DEFAULT_ALLOW = (
    r"git\s+(status|diff|log|show|ls-files|remote|branch)\b"
    r"|^(ls|cat|head|tail|wc|file|stat|pwd|which|find|rg|grep|sed\s+-n|awk|sort|uniq|cut|tr|basename|dirname)\b"
)

HERE = os.path.dirname(os.path.abspath(__file__))
CONF = os.path.join(os.path.dirname(HERE), "agy.conf")


def conf():
    out = {}
    try:
        with open(CONF, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


def state_path(payload):
    cid = str(payload.get("conversationId") or "nocid")
    cid = re.sub(r"[^A-Za-z0-9_-]", "", cid)[:64] or "nocid"
    d = os.path.join(tempfile.gettempdir(), "agy-gate")
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, cid)


def state(payload):
    try:
        with open(state_path(payload), encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def save(payload, st):
    try:
        with open(state_path(payload), "w", encoding="utf-8") as fh:
            json.dump(st, fh)
    except OSError:
        pass


def workspace(payload):
    paths = payload.get("workspacePaths") or []
    return paths[0] if paths else os.path.dirname(os.path.dirname(HERE))


def tree_hash(root):
    """Fingerprint the working tree. None when this is not a git repository.

    `git status --porcelain` alone is not enough: editing an already-modified
    file again leaves the status output byte-identical, so the gate would miss
    the second edit. The tracked diff is hashed alongside it.
    """
    parts = []
    for args in (["status", "--porcelain"], ["diff", "HEAD"]):
        try:
            r = subprocess.run(["git", "-C", root] + args,
                               capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            return None
        if r.returncode != 0:
            # `git diff HEAD` fails in a repository with no commits yet; status
            # alone still tells us something there.
            if args[0] == "status":
                return None
            continue
        parts.append(r.stdout)
    return hashlib.md5("\x00".join(parts).encode()).hexdigest()


def emit(obj):
    json.dump(obj, sys.stdout)
    sys.stdout.write("\n")


PLAN_CMD = "sh .agents/hooks/plan.sh"
PLAN_MAX_DENIALS = 3


def pre_edit(payload, c):
    """Refuse the first edit of a session until a plan exists.

    Returning `{}` means "no opinion" and leaves agy's normal permission flow
    alone; measured 2026-08-26. Only an explicit decision overrides it.
    """
    if (c.get("PLAN_GATE") or "off").lower() != "on":
        return {}
    st = state(payload)
    if st.get("plan") or st.get("plan_denials", 0) >= PLAN_MAX_DENIALS:
        return {}
    st["plan_denials"] = st.get("plan_denials", 0) + 1
    save(payload, st)
    return {"decision": "deny",
            "reason": ("Plan before editing. Record one first, in a single command:\n"
                       f"  {PLAN_CMD} 'objective | files | approach | verification command'\n"
                       "Then make this edit again. Keep it to one line; this is a "
                       "commitment about what you are about to do, not a design document.")}


def pre_tool(payload, c):
    cmd = ((payload.get("toolCall") or {}).get("args") or {}).get("CommandLine") or ""
    if not cmd:
        return {"decision": "ask"}

    deny = DEFAULT_DENY
    if c.get("DENY_RE"):
        deny += "|" + c["DENY_RE"]
    if re.search(deny, cmd):
        return {"decision": "deny",
                "reason": ("Blocked by the project's agy guard: this destroys or publishes "
                           "work that may not be yours. If you truly need it, ask the user "
                           "and run it yourself.")}

    if cmd.strip().startswith(PLAN_CMD) or "hooks/plan.sh" in cmd:
        return {"decision": "allow", "reason": "recording the plan"}

    verify_re = c.get("VERIFY_RE")
    if verify_re and re.search(verify_re, cmd):
        st = state(payload)
        st["verify_pending"] = cmd
        save(payload, st)
        return {"decision": "allow", "reason": "project verification command"}

    allow = c.get("ALLOW_RE") or DEFAULT_ALLOW
    if re.search(allow, cmd):
        return {"decision": "allow", "reason": "read-only inspection"}

    return {"decision": "ask"}


def post_tool(payload, _c):
    st = state(payload)
    pending = st.pop("verify_pending", None)
    if pending:
        if payload.get("error"):
            st["verify_failed"] = pending
        else:
            st["verified"] = pending
        save(payload, st)
    return {}


def pre_invoke(payload, _c):
    st = state(payload)
    if "baseline" not in st:
        st["baseline"] = tree_hash(workspace(payload)) or ""
        save(payload, st)
    return {}


def stop(payload, c):
    st = state(payload)
    # Never fight an error, a step limit, a cancellation, or unfinished work.
    # Measured 2026-08-26: an ordinary model stop reports terminationReason
    # "NO_TOOL_CALL", not the "model_stop" the bundled docs show — so this is a
    # denylist of reasons we must not argue with, never an allowlist of one.
    reason = str(payload.get("terminationReason") or "").lower()
    if payload.get("error"):
        return {"decision": "stop"}
    if any(w in reason for w in ("error", "cancel", "abort", "max_step", "user", "timeout", "interrupt")):
        return {"decision": "stop"}
    if payload.get("fullyIdle") is False:
        return {"decision": "stop"}
    if st.get("forced", 0) >= 1:
        return {"decision": "stop"}

    verify_cmd = c.get("VERIFY_CMD")
    if not verify_cmd or st.get("verified"):
        return {"decision": "stop"}

    baseline = st.get("baseline")
    now = tree_hash(workspace(payload))
    if baseline is None or now is None or baseline == "" or now == baseline:
        return {"decision": "stop"}

    st["forced"] = st.get("forced", 0) + 1
    save(payload, st)
    failed = st.get("verify_failed")
    tail = (f" `{failed}` ran and failed — report that as FAIL rather than stopping quietly."
            if failed else
            f" Run `{verify_cmd}` and report PASS or FAIL, or state plainly why it is BLOCKED.")
    return {"decision": "continue",
            "reason": ("Files changed in this session but no verification has succeeded." + tail)}


def record_plan(payload, _c):
    """Called by plan.sh, not by agy: argv[2] carries the plan text."""
    st = state(payload)
    st["plan"] = (sys.argv[2] if len(sys.argv) > 2 else "").strip()[:2000]
    save(payload, st)
    return {"recorded": bool(st["plan"])}


HANDLERS = {"pre-tool": pre_tool, "pre-edit": pre_edit, "post-tool": post_tool,
            "pre-invoke": pre_invoke, "stop": stop, "record-plan": record_plan}


def main():
    event = sys.argv[1] if len(sys.argv) > 1 else ""
    # On any failure the hook must get out of the way: `{}` for the edit gate,
    # a prompt for an unclassified command, and never a blocked termination.
    fallback = {"decision": "ask"} if event == "pre-tool" else (
        {"decision": "stop"} if event == "stop" else {})
    try:
        if event == "record-plan":
            # plan.sh has no hook payload; the conversation id comes from the
            # environment agy exports into the tool's shell.
            payload = {"conversationId": os.environ.get("ANTIGRAVITY_CONVERSATION_ID", "")}
            emit(record_plan(payload, conf()))
            return
        raw = sys.stdin.read()
        payload = json.loads(raw) if raw.strip() else {}
        handler = HANDLERS.get(event)
        emit(handler(payload, conf()) if handler else fallback)
    except Exception:
        emit(fallback)


if __name__ == "__main__":
    main()
