#!/bin/bash
# يعرض فقط رسائل ##[error] من آخر run.

cd /storage/emulated/0/YAM-GG || exit 1
RID=$(gh run list --repo Yamin843/YAM-GG --limit 1 --json databaseId --jq '.[0].databaseId')
gh run view $RID --repo Yamin843/YAM-GG --log 2>&1 | \
    grep -E "##\[error\]|error:|Error:|undefined reference|BUILD FAILED" | \
    sed -E 's/^[^ ]+ +[A-Za-z0-9_ -]+ +[0-9T:.Z-]+ +//' | \
    head -50
