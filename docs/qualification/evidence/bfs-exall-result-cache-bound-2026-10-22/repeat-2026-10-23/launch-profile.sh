#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# The user authorized temporary stop/restore of this idle runner and Coffin.
set -euo pipefail
workspace=${1:?Usage: launch-profile.sh WORKSPACE}
[[ ! -e "$workspace/results" ]]
[[ $(docker inspect -f '{{.State.Running}}' snapdog-runner) == true ]]
[[ $(systemctl --user show coffin.service -p ActiveState --value) == active ]]
worker_list=$(docker top snapdog-runner -eo pid,ppid,args)
if printf '%s\n' "$worker_list" | rg -q 'Runner\.Worker'; then
    printf 'ERROR: refusing to stop an active CI job\n' >&2
    exit 1
fi
docker top snapdog-runner -eo pid,ppid,args >"$workspace/runner-before.log"
systemctl --user show coffin.service -p LoadState -p ActiveState -p SubState -p MainPID \
    >"$workspace/coffin-before.log"
runner_stopped=0
coffin_stopped=0
restore() {
    local status=$?
    trap - EXIT
    if [[ $runner_stopped == 1 ]]; then
        if ! docker start snapdog-runner; then status=1; fi
    fi
    if [[ $coffin_stopped == 1 ]]; then
        if ! systemctl --user start coffin.service; then status=1; fi
    fi
    docker inspect -f '{{.State.Running}} {{.State.Status}}' snapdog-runner \
        >"$workspace/runner-after.log" || status=1
    systemctl --user show coffin.service -p LoadState -p ActiveState -p SubState -p MainPID \
        >"$workspace/coffin-after.log" || status=1
    [[ $(docker inspect -f '{{.State.Running}}' snapdog-runner) == true ]] || status=1
    [[ $(systemctl --user show coffin.service -p ActiveState --value) == active ]] || status=1
    printf 'RESTORE_COMPLETE status=%s %s\n' "$status" "$(date -u +%FT%TZ)"
    exit "$status"
}
trap restore EXIT
runner_stopped=1
docker stop --timeout 30 snapdog-runner
coffin_stopped=1
systemctl --user stop coffin.service
bash "$workspace/run-profile.sh" "$workspace"
