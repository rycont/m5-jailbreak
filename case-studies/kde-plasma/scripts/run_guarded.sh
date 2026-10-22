#!/bin/sh
# usage: run_guarded.sh <timeout_sec> <cmd...>
# Runs cmd with a hard timeout, then TERM/KILL; always reaps the child.
t="$1"; shift
"$@" &
pid=$!
( sleep "$t"; kill -TERM "$pid" 2>/dev/null; sleep 2; kill -KILL "$pid" 2>/dev/null ) &
wd=$!
wait "$pid"; st=$?
kill "$wd" 2>/dev/null
wait "$wd" 2>/dev/null
exit "$st"
