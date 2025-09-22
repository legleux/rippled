#!/usr/bin/env sh

set -x
. /etc/os-release
/opt/ripple/bin/rippled --unittest "ripple.app.NFTokenBurn" --unittest-jobs $(nproc) 2>&1 | tee unittest_results
cat unittest_results
CI_JOB_URL=some_url
num_failures=$(tail unittest_results -n1 | grep -oP '\d+(?= failures)')
if [ "${num_failures}" -ne 0 ]; then
    echo "${num_failures} tests failed!"
    read -r -d '' CONTENT <<EOF
    payload={
        "username": "GitlabCI",
        "text": "rippled failed ${num_failures} unittests on ${ID} ${VERSION_ID}\nResults at ${CI_JOB_URL}",
        "icon_emoji": ":boom:"}
EOF
    echo curl "${SLACK_NOTIFY_URL}" --data-urlencode "${CONTENT}"
fi
