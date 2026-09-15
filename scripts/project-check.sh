#!/bin/sh
set -eu

fail() {
  printf '%s\n' "project-check: $*" >&2
  exit 1
}

if [ -x scripts/check-tools.sh ]; then
  scripts/check-tools.sh
fi

has_git=0
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  has_git=1
  git status --short >/dev/null
  git diff --check
else
  printf '%s\n' "project-check: note: no git metadata; version-control checks skipped"
fi

# The default context (AGENTS.md + CONTEXT.md) must stay within budget.
if [ -x scripts/context-budget.sh ]; then
  scripts/context-budget.sh || fail "default context over budget"
fi

if command -v rg >/dev/null 2>&1; then
  private_pattern='(/ho''me/|/Us''ers/|/m''nt/|ssh -''i|BEGIN[[:space:]][A-Z0-9[:space:]]*PRI''VATE[[:space:]]KEY)'
  # data: URI 로 박은 글꼴·이미지(base64)는 그 알파벳 탓에 경로처럼 보인다 --- 오탐이다
  hits=$(rg -n "$private_pattern" . --glob '!.git/**' | grep -v ';base64,' || true)
  [ -z "$hits" ] || { printf '%s\n' "$hits"; fail "private path or key-like pattern found"; }
fi

# 작업공간 공용 검사가 있으면 함께 돌린다 --- 추적 파일과(인자를 주면) 빌드 산출물에서
# 이 기계의 절대 경로와 인증서 꼴을 찾는다. 없으면 조용히 건너뛴다.
# 그 자리에서 옳은 문자열은 저장소 뿌리의 .privacy-allow 에 한 줄씩 적는다.
privacy="$(cd "$(dirname "$0")/.." && pwd)/../usr/bin/check-privacy"
if [ "$has_git" -eq 1 ] && [ -x "$privacy" ]; then
  "$privacy" || fail "local paths or credentials in the repository"
fi

if command -v find >/dev/null 2>&1; then
  for script in $(find scripts -type f -name '*.sh' 2>/dev/null | sort); do
    first=$(sed -n '1p' "$script")
    case "$first" in
      *bash*) command -v bash >/dev/null 2>&1 || fail "bash is required for $script"; bash -n "$script" ;;
      *) sh -n "$script" ;;
    esac
  done
fi

# The version must live in one place and be derived everywhere else.
if command -v python3 >/dev/null 2>&1 && [ -x scripts/check-version.py ]; then
  python3 scripts/check-version.py || fail "the version disagrees with itself"
fi

# §11.2's M9 criterion, checked mechanically: a setting some module reads
# but no tab offers is a setting the reader cannot change.
if command -v python3 >/dev/null 2>&1 && [ -x scripts/check-settings.py ]; then
  python3 scripts/check-settings.py || fail "a setting is not reachable from the settings window"
fi

# D-13: every configuration file the viewer writes is valid TOML and valid
# INI with the same meaning — measured with both parsers, not asserted.
if command -v python3 >/dev/null 2>&1 && command -v cc >/dev/null 2>&1 && [ -x scripts/check-conf-format.py ]; then
  python3 scripts/check-conf-format.py || fail "a configuration file is not in the INI and TOML subset"
fi

# RFC-0003 / D-16: every key, tile and menu item is handled, and everything
# handled is reachable.
if command -v python3 >/dev/null 2>&1 && [ -x scripts/check-actions.py ]; then
  python3 scripts/check-actions.py || fail "an action is bound but not handled, or handled but not reachable"
fi

printf '%s\n' "project-check: ok"
