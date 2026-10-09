#!/bin/bash
set -e

cd /storage/emulated/0/YAM-GG

git config --global user.email "yamin843@users.noreply.github.com"
git config --global user.name "Yamin843"
git config --global credential.helper store

if [ ! -d .git ]; then
    git init
fi

git remote remove origin 2>/dev/null || true
git remote add origin https://github.com/Yamin843/YAM-GG.git

git add -A
git commit -m "Initial YAM-GG project" || true

echo "To push:"
echo "  git push -u origin main --force"
