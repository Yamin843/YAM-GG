#!/bin/bash
# يعرض الأخطاء الحقيقية فقط، ويلغي الضوضاء.

cd /storage/emulated/0/YAM-GG || exit 1

RID="${1:-$(gh run list --repo Yamin843/YAM-GG --limit 1 --json databaseId --jq '.[0].databaseId')}"

if [ -z "$RID" ] || [ "$RID" = "null" ]; then
    echo "لا يوجد run"
    exit 1
fi

LOG=/storage/emulated/0/YAM-GG/build_failure.log
echo "RUN_ID=$RID"
echo "log: $LOG"
echo ""

gh run view $RID --repo Yamin843/YAM-GG --log > "$LOG" 2>&1

# دالة مساعدة لطباعة قسم
print_section() {
    echo ""
    echo "═══════════════════════════════════════════"
    echo "  $1"
    echo "═══════════════════════════════════════════"
}

# الفلتر: يزيل الطوابع الزمنية، يزيل أسطر التقدم، يحتفظ بالمعنى
clean() {
    sed -E 's/^[^ ]+ +[A-Za-z0-9_ -]+ +[0-9T:.Z-]+ +//' | \
    grep -vE '^\[[= ]+\]' | \
    grep -vE '^\[command\]' | \
    grep -vE 'Loading (local|remote) repository' | \
    grep -vE 'Computing updates' | \
    grep -vE '^[[:space:]]*$' | \
    grep -vE 'DeprecationWarning|punycode|trace-deprecation' | \
    grep -vE '^\+ ' | \
    grep -vE 'actions/(checkout|setup-java|upload-artifact|download-artifact)' | \
    grep -vE '^Post (Checkout|Setup Java|Setup Android)' | \
    grep -vE '^\s*git (version|config|submodule)' | \
    grep -vE 'Temporarily overriding HOME' | \
    grep -vE 'Adding repository directory to' | \
    grep -vE 'safe.directory'
}

print_section "1. الخطوة التي فشلت"
grep -E "##\[error\]|Process completed with exit|Error: " "$LOG" | \
    clean | head -10

print_section "2. الأخطاء الفعلية (error: | Error:)"
grep -E "error:|Error:|error [A-Z]|FAILED:" "$LOG" | \
    clean | head -50

print_section "3. أخطاء DEX / D8 / JAVAC"
grep -iE "javac|d8:|d8 |dex|class file|InMemoryDex|--no-desugaring" "$LOG" | \
    clean | head -30

print_section "4. undefined reference (لينكر)"
grep -E "undefined reference|cannot find -l|unresolved symbol" "$LOG" | \
    clean | head -30

print_section "5. ملفات مفقودة"
grep -E "No such file|not found|cannot open|does not exist" "$LOG" | \
    clean | grep -vE "sdkmanager|cmdline-tools|Setting up|Checking" | head -20

print_section "6. رسائل sdkmanager (لو فشلت)"
grep -E "Warning: Failed to find package|Failed to install|Could not" "$LOG" | \
    clean | head -10

print_section "7. آخر 40 سطر من خطوة البناء"
# استخراج قسم ndk-build
awk '/Run .*ndk-build|Build .so with ndk-build/,0' "$LOG" | tail -40 | clean

print_section "8. ملخص"
if grep -q "BUILD SUCCESSFUL\|Build Succeeded\|finished successfully" "$LOG"; then
    echo "✓ البناء نجح — لا توجد أخطاء"
else
    echo "✗ البناء لم ينجح"
fi

echo ""
echo "📁 log كامل: $LOG"
echo ""
echo "لتصدير قسم واحد فقط:"
echo "  grep -A50 'Build .so with ndk-build' $LOG | head -60"
