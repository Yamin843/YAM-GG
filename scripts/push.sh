#!/bin/bash
set -e
cd /storage/emulated/0/YAM-GG

bash scripts/verify.sh || {
    echo ""
    echo "التحقق فشل — أصلح الملفات المفقودة أولاً"
    exit 1
}
echo ""

if [ ! -d .git ]; then
    git init
    git branch -M main
fi

git config user.email "yamin843@users.noreply.github.com"
git config user.name "Yamin843"

git remote remove origin 2>/dev/null || true
git remote add origin https://github.com/Yamin843/YAM-GG.git

git add -A
echo ""
echo "الملفات المضافة:"
git status --short | head -80
echo ""

git commit -m "YAM-GG complete: dex + GLSurfaceView + ImGui 1.90.9 + JS console" || echo "(لا جديد)"

echo ""
echo "لرفع المشروع:"
echo "  git push -u origin main --force"
