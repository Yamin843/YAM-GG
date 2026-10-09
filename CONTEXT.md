YAM-GG — التوثيق الشامل (النسخة المُدمَجة النهائية)

بديل كامل عن READMEFULL.md و READMEFULL2.md
يشمل: كل ما سبق + التعديلات التي طُبقت + قواعد العمل الصارمة

---

القسم 0 — الطرفان وسياق العمل

0.1 من أنا (Claude)

· مساعد ذكاء اصطناعي من Anthropic، أعمل عبر واجهة محادثة.
· دوري: مهندس كود، مُراجع، منفّذ تعديلات عبر أوامر Termux ينسخها المستخدم.
· ما لا أفعله: لا أُشغّل أوامر فعلياً، لا أرى شاشتك، لا أُصلح ملفات بدون أوامر.
· قدراتي: تحليل كود كامل (C++/Java/JS/Shell)، كتابة أوامر Python/bash، توليد أكواد JS.

0.2 من أنت (المستخدم)

· مالك مشروع YAM-GG — أداة حقن ImGui + JS console لتطبيقات Android.
· بيئة العمل: Termux على جهاز Android، البناء يجري على GitHub Actions.
· أسلوبك: صارم، دقيق، لا تقبل التخمين، تعرف مشروعك أفضل من أي أحد.
· الأدوات: gh CLI، adb، محرر نصوص، أدوات حقن (بدون تسمية محددة).

0.3 طبيعة المشروع

YAM-GG: مكتبة .so تُحقن في تطبيق Android، فتظهر واجهة ImGui عائمة مع:

· تبويبات جانبية للتنقل
· متصفح فئات/دوال Java
· كونسول JS حي للتنصت والاستدعاء
· دعم كامل لأنواع بارامترات Java

المكونات:

```
libyamjs.a  ← frida-gum 17.22 مُعاد تسميته + java-bridge مدمج
wrapper/    ← طبقة C++17 (yam:: namespace)
project/    ← Main.cpp + JNI + UI + Bridge + ModView.java
```

---

القسم 1 — قواعد العمل الصارمة

1.1 قواعد عامة (يجب على Claude الالتزام بها)

# القاعدة
G1 لا تستخدم /tmp — لا يعمل في Termux
G2 لا تُبسّط ولا تختزل — اكتب كوداً كاملاً حقيقياً
G3 لا تستخدم stubs — كل دالة تُكتب تُنفَّذ فعلاً
G4 تحقّق قبل التعديل — من الرؤوس والتوثيق والمكتبة
G5 لا تُؤجّل شيئاً لردود أخرى إن طُلب منك إنجازه
G6 الاعتذار عند الفشل، لا التبرير
G7 لا تعتمد على الجولات السابقة — أعد القراءة من المصدر
G8 الردود تحتوي كتل تنفيذية جاهزة للنسخ

1.2 قواعد تقنية

# القاعدة
T1 كل تعديل على ملف = نسخة .bak قبل التعديل
T2 كل تعديل = تعديل واحد مترابط (لا خلط تغييرات لا علاقة لها)
T3 التحقق من توازن الأقواس بعد كل تعديل نصي ({, (, [ )
T4 عدم استخدام NUL byte داخل JavaScript
T5 تجنّب حرف [ و ] داخل نص JS — استخدم \u005B \u005D
T6 عدم لمس الجسر المدمج في libyamjs.a
T7 الأسماء في libyamjs.a مُعاد تسميتها (g_bytes_new → _yamfl_g_bytes_new)
T8 أي استدعاء GLib من خارج الواربر يفشل (#define غير متاح في Main.cpp)
T9 قبل الحذف، تحقق من عدم وجود مراجع متبقية
T10 بعد حذف ملف، حدّث Android.mk

1.3 قواعد التحقق قبل التعديل

```
1) ابحث في YAMJS.h عن التوقيع الفعلي
2) ابحث في libyamjs.a عن الرمز الفعلي
3) تحقّق من الـ namespace (yam:: vs global)
4) تحقّق من الـ #define للأسماء المُعاد تسميتها
5) تحقّق من الـ JS bridge (java-bridge.js) لسلوك recv/send
```

1.4 تنبيهات حرجة

التنبيه السبب
Script::post يحتاج الغلاف الكامل JSON كـ message message-dispatcher يحلل message بـ JSON.parse
yam_script_post يعمل فقط خلال Script::load الطابور لا يُعالَج بعدها
كل استدعاء من C→JS بعد load يحتاج polling من JS أو nativeGetPendingCmd
Java.use من classloader خاطئ = ClassNotFoundException يجب ضبط Java.classFactory.loader
Java.choose ثقيل على Unity استخدمه كل N محاولات
breakpoint في الجسر يوقف JS لا تستخدمه في trace البسيط
WantCaptureMouse في ImGui يحدد استهلاك اللمس للتمرير الشفاف
new GLSurfaceView خارج UI thread = crash استخدم runOnUiThread

---

القسم 2 — صيغة الرد المعتمدة

2.1 بنية الرد

```
1. تشخيص موجز لما فُهم (1-3 أسطر)
2. جدول القرار إن لزم
3. كتل تنفيذية مرقّمة:
   - الكتلة 1
   - الكتلة 2
   - ...
4. الأوامر الثلاثة (في النهاية دائماً)
5. توقّع النتيجة (اختياري)
```

2.2 صيغة الكتلة

```
## الكتلة N — وصف موجز

\```
cd /storage/emulated/0/YAM-GG || exit 1
# كود bash/python
\```
```

2.3 قواعد الكتل

· الكتلة تُنفَّذ من جذر المشروع دائماً
· استخدام cd ... || exit 1 للأمان
· استخدام python3 << 'PYEOF' ... PYEOF للتعديلات المعقدة
· تحقق في نهاية كل كتلة (grep, echo)
· لا كتلة عملاقة تتجاوز ~200 سطر

---

القسم 3 — الأوامر الثلاثة الإلزامية

تُرفَق في نهاية كل رد — حتى لو لم تُطلب.

3.1 الكتلة 1 — سحب لوغ الفشل (عند فشل البناء)

```bash
cd /storage/emulated/0/YAM-GG || exit 1
mkdir -p _work
RID=$(gh run list --repo Yamin843/YAM-GG --limit 1 --json databaseId --jq '.[0].databaseId')
echo "RUN_ID=$RID"
gh run view $RID --repo Yamin843/YAM-GG --log > _work/ci_last.log 2>&1
echo ""
echo "=== errors ==="
grep -E "##\[error\]|error:|undefined reference|BUILD FAILED|FAILED:" _work/ci_last.log | head -40
echo ""
echo "log: _work/ci_last.log ($(wc -l < _work/ci_last.log) lines)"
```

3.2 الكتلة 2 — تنزيل .so (عند نجاح البناء)

```bash
cd /storage/emulated/0/YAM-GG || exit 1
RID=$(gh run list --repo Yamin843/YAM-GG --status success --limit 1 --json databaseId --jq '.[0].databaseId')
if [ -z "$RID" ] || [ "$RID" = "null" ]; then echo "NO_SUCCESS_RUN"; exit 1; fi
mkdir -p _work/artifact && rm -rf _work/artifact/*
gh run download $RID --repo Yamin843/YAM-GG --name YAMGG-so -D _work/artifact
SO=$(find _work/artifact -name "libYAMGG.so" | head -1)
[ -z "$SO" ] && { echo "NO_SO"; exit 1; }
cp -f "$SO" /storage/emulated/0/libYAMGG-arm64.so
chmod 644 /storage/emulated/0/libYAMGG-arm64.so
echo ""
echo "═══════════════════════════════════════════"
echo "  ✅ SO: /storage/emulated/0/libYAMGG-arm64.so"
echo "  📏 $(du -h /storage/emulated/0/libYAMGG-arm64.so | cut -f1)"
echo "═══════════════════════════════════════════"
```

3.3 الكتلة 3 — سحب logcat (بعد الاختبار)

```bash
LOG=/storage/emulated/0/Download/yamgg.log
pkill -f logcat 2>/dev/null; sleep 1; rm -f "$LOG"; logcat -c
nohup logcat -v threadtime \
    YAMGG:V YAMGG-DBG:V YAM:V AndroidRuntime:V DEBUG:V libc:V '*:S' > "$LOG" 2>&1 &
echo "احقن الآن — 40 ثانية"
sleep 40
pkill -f logcat
echo ""
echo "=== آخر 30 سطر ==="
tail -30 "$LOG"
```

---

القسم 4 — نظرة عامة على المعمارية

4.1 الطبقات

```
┌─────────────────────────────────────────────────────────┐
│ Android App (Java)                                      │
│   ModView.java, ModViewHelper.java                      │
└───────────────────┬─────────────────────────────────────┘
                    │ JNI
┌───────────────────▼─────────────────────────────────────┐
│ Main.cpp — JNI_OnLoad                                   │
│   ├── init_thread                                       │
│   ├── DexLoader::loadEmbeddedDex                        │
│   ├── YamBridge::initialize (فريدا)                     │
│   └── GMainContext pump loop (يُبقي JS يعمل)            │
└───────────────────┬─────────────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────────────┐
│ wrapper/ (yam:: namespace)                              │
│   JavaScriptBridge, JavaFacade, JavaHookManager         │
│   Runtime, Memory, Module, Interceptor, ...             │
└───────────────────┬─────────────────────────────────────┘
                    │ C API (yam_*)
┌───────────────────▼─────────────────────────────────────┐
│ libyamjs.a — frida-gum 17.22 + java-bridge              │
└───────────────────┬─────────────────────────────────────┘
                    │ QuickJS / V8
┌───────────────────▼─────────────────────────────────────┐
│ java-bridge.js (QJS prelude)                            │
│   - globalThis.Java                                     │
│   - recv("cmd", dispatch)                               │
│   - send({type, ...})                                   │
│   - rpc.exports.*                                       │
└───────────────────┬─────────────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────────────┐
│ bootstrap_js.h (JsPrelude الخاص بنا)                    │
│   - recv("yamgg_cmd") للـ cpp_* commands                │
│   - poller لقراءة nativeGetPendingCmd                   │
│   - attach loop + onResume hook                         │
│   - describe() لتحويل Java objects                      │
└─────────────────────────────────────────────────────────┘
```

4.2 بروتوكول الاتصال

C++ → JS (مشكلة سابقة، حُلَّت)

الطريقة القديمة: yam_script_post(handle, "type", payload_gbytes) — لا يعمل بعد load.

الحل المعتمد:

1. Script::post(envelope) → yamgg_postCommand → g_cmdQueue
2. JS setInterval(80ms) يقرأ ModView.nativeGetPendingCmd() (JNI)
3. JNI يعيد عنصراً من الطابور
4. JS يحلل JSON → handlers[action]

JS → C++ (يعمل دائماً)

send({type, ...}) → script_msg_trampoline → JavaScriptBridge::on_event_json → events::dispatch

cpp_* commands (reply envelope)

```
C++ → [queue] → JS → recv("yamgg_cmd")
JS → send({type:"reply", payload:"{id, ok, kind, handle}"})
C++ → on_reply → pending_[id] → المُرجع
```

---

القسم 5 — المكتبة المعدلة (libyamjs.a)

5.1 ما هي

· frida-gum 17.22.x بعد إعادة تسمية شاملة
· تحوي java-bridge كـ bytecode مدمج
· الحجم: ~120 MB
· المعمارية: arm64-v8a فقط

5.2 إعادة التسمية

الأصلي المُعدَّل القاعدة
gum_* yam_* بادئة C
Gum* Yam* أسماء C++
Gum:: Yam:: namespace
g_bytes_new _yamfl_g_bytes_new عبر #define في YAMJS.h
g_bytes_unref _yamfl_g_bytes_unref نفس الأسلوب

5.3 الرموز الأساسية

Script Engine

```c
yam_init, yam_init_embedded, yam_deinit_embedded
yam_script_backend_obtain_qjs, yam_script_backend_obtain_v8
yam_script_backend_create_sync, yam_script_backend_create_from_bytes_sync
yam_script_load_sync, yam_script_unload_sync
yam_script_post(YamScript*, const gchar* message, GBytes* data)
yam_script_set_message_handler(script, handler, data, destroy)
```

Interceptor

```c
yam_interceptor_obtain, yam_interceptor_attach, yam_interceptor_detach
yam_interceptor_replace, yam_interceptor_revert
```

Memory

```c
yam_memory_allocate(address, size, alignment, prot)   // 4 params
yam_memory_allocate_near(spec, size, alignment, prot)
yam_memory_free(address, size)
yam_memory_read(addr, size, &bytes_read) -> guint8*
yam_memory_write(addr, data, size) -> gboolean
yam_memory_query_protection, yam_memory_query_region
```

Module

```c
yam_process_find_module_by_name(name)
yam_module_find_export_by_name(module, name)
yam_module_find_global_export_by_name(name)
yam_module_get_name, yam_module_get_path, yam_module_get_range
```

InvocationContext

```c
yam_invocation_context_get_nth_argument(ctx, n)
yam_invocation_context_replace_nth_argument(ctx, n, val)
yam_invocation_context_get_return_value(ctx)
yam_invocation_context_get_thread_id(ctx)
```

GBytes (مُعاد تسميته)

```c
g_bytes_new → _yamfl_g_bytes_new
g_bytes_unref → _yamfl_g_bytes_unref
```

---

القسم 6 — الجسر المدمج (java-bridge.js)

6.1 الواجهات المُعرَّضة

```javascript
globalThis.Java           // كل واجهات Java bridge
globalThis.Agent          // نظام إضافات (غير مؤكَّد)
rpc.exports.*             // نقاط دخول RPC
recv("cmd", dispatch)     // قناة الأوامر الأصلية
send({type, ...})         // إرسال للـ C++
```

6.2 الأوامر الأصلية (90+)

مصنَّفة في: tracing, breakpoints, watches, chunks, user scripts, calls, fields, paths, handles, native, Java hooks, deopt, auto, console, agent.

6.3 الـ events المُرسَلة (45+)

مصنَّفة في: lifecycle, tracing, breakpoints, watches, chunks, calls, fields, paths, enumeration, handles, env, scripts, replay, console, hooks.

6.4 قيود حرجة

القيد السبب
لا تُسجّل recv("cmd") جديد تستبدل المعالج الأصلي
Agent.registerCommand قد لا يعمل يعتمد على نسخة الجسر
القناة المخصصة yamgg_cmd تعمل لأن JS bootstrap يعالجها
الردود تمر بـ {type:"reply", payload:"..."} envelope معتمد

---

القسم 7 — الواربر C++ (yam:: namespace)

7.1 الملفات

الملف الوصف
yam.hpp واجهة عامة شاملة (~1200 سطر)
yam_core.cpp أساسيات + Runtime + Memory + Module
yam_hooks.cpp Interceptor + InvocationContext
yam_memory.cpp ذاكرة + Module + Symbol
yam_java.cpp JavaScriptBridge + bootstrap
yam_java_model.cpp JavaClass/Method/Field/Instance
yam_events.cpp Event router + BP dispatch
yam_console.cpp Console + Entry + Diag
yam_subsystems.cpp CModule, Cloak, ElfModule, ...
yam_stalker.cpp Stalker wrapper
yam_json.cpp JSON parser يدوي
bootstrap_js.h JS prelude (نُولِّده بايثون)

7.2 النقاط الحرجة بعد التعديلات

Script::post (نسخة نهائية)

```cpp
void Script::post(const String& msg) {
    // تمرير الغلاف الكامل كـ message
    // message-dispatcher JS يحلل JSON
    yam_script_post(static_cast<YamScript*>(handle_), msg.c_str(), nullptr);
}
```

yamgg_pump_once (وصول GMainContext)

```cpp
extern "C" void yamgg_pump_once() {
    GMainContext* ctx = g_main_context_get_thread_default();
    if (!ctx) ctx = g_main_context_default();
    if (!ctx) return;
    while (g_main_context_iteration(ctx, FALSE)) {}
}
```

هذه الدالة في yam_core.cpp (حيث #define يعمل)، تُستدعى من Main.cpp عبر extern "C".

---

القسم 8 — كود المشروع

8.1 Main.cpp

الدور:

· JNI_OnLoad: يُشغّل init_thread
· init_thread: DexLoader + YamBridge + pump loop
· yamgg_postCommand: يُدرِج في g_cmdQueue
· nativeGetPendingCmd: JNI method — JS يقرأ من الطابور

القواعد:

· لا تضمّن glib.h — ليس في include path
· استخدم yamgg_pump_once() (خارج الواربر)
· كل GSymbol من مكتبة → تمرير عبر extern "C"

8.2 ModView.java

الميزات:

· GLSurfaceView + ImGui renderer
· sPendingCmd / sCmdSeq (تَجاوَزناها، لم تُستخدم)
· nativeGetPendingCmd — يقرأ من الطابور
· sHiddenInput (EditText شفاف) — لالتقاط IME
· syncSoftKeyboard — يظهر/يخفي لوحة المفاتيح حسب WantTextInput
· onTouchEvent → nativeOnTouch (تمرير WantCaptureMouse)

8.3 الواجهة (ImGui)

MainWindow:

· Sidebar (110px) بتبويبات: JV / JS
· 5-zones للتحكم (top/bottom/left/right/corner)
· Scrollbar مخفي
· SetNextWindowSizeConstraints(650x450, viewport)

ClassBrowser:

· Search bar: checkboxes (Class / Method / Field) + input + Find
· Tree: Package → Class → Fields table → Methods
· لكل method: params widgets + Trace/Call + instances
· كل النصوص: TextWrapped + PushTextWrapPos(0)

JSConsole:

· Sub-tabs: LOG (مع Clear) / Console
· Console tab: Load/Unload/fromSD + Editor + Scripts list
· لا hints أو تعليمات

FileBrowser:

· Breadcrumb path قابل للنقر (لا أزرار Home/Refresh/...)
· عرض الملفات المخفية افتراضياً
· Folders قابلة للنقر للدخول

Theme:

· GrabMinSize = 56
· ScrollbarSize = 0
· Border 5px ذهبي

8.4 ModViewHelper.java

يخزّن ClassLoader + Activity lifecycle.

---

القسم 9 — نظام البناء

9.1 Application.mk

```makefile
APP_ABI := arm64-v8a
APP_PLATFORM := android-24
APP_STL := c++_static
APP_OPTIM := release
```

9.2 Android.mk الرئيسي

· LOCAL_MODULE := YAMGG
· يضم كل ملفات wrapper/src/*.cpp
· prebuilt: libyamjs.a, libdobby.a, libkeystone.a, libasmjit.a, libkitty_memory.a, libxdl.a
· LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -ldl -lz -lm
· -Wl,--gc-sections -Wl,--exclude-libs,ALL
· -DYAM_ARCH_ARM64=1

9.3 build.gradle

```gradle
android {
    compileSdk 34
    ndkVersion '26.1.10909125'
    defaultConfig {
        minSdk 24
        ndk { abiFilters 'arm64-v8a' }
    }
    externalNativeBuild {
        ndkBuild { path 'src/main/jni/Android.mk' }
    }
}

tasks.register('generateDex', Exec) {
    workingDir rootProject.projectDir
    commandLine 'bash', 'scripts/generate-dex.sh'
}
tasks.matching { it.name.startsWith('externalNativeBuild') || it.name == 'preBuild' }
     .configureEach { dependsOn 'generateDex' }
```

9.4 scripts/generate-dex.sh

1. javac على ModView.java + ModViewHelper.java
2. d8 → classes.dex
3. dex_to_header.py → embedded_dex.h

9.5 .github/workflows/build.yml

· Reassemble libyamjs.a من أجزاء
· Generate embedded dex
· Build .so with ndk-build
· Locate .so + Upload

---

القسم 10 — تاريخ التعديلات

10.1 الجولات الحرجة

الجولة التعديل الأثر
1 bootstrap في ملف .h منفصل تجاوز NUL bytes
2 Java.performNow للـ loader كسر حلقة perform
3 attach_now عبر scheduleOnMainThread واجهة فورية
4 mActivities + pickBestActivity اكتشاف activity
5 Script::post envelope كامل C→JS يعمل
6 queue + nativeGetPendingCmd C→JS عبر polling
7 5-zone drag/resize تحكم مخصص
8 ملفات ClassBrowser / JSConsole جديدة واجهة جديدة

10.2 الأخطاء المتكررة التي حُلَّت

# الخطأ الحل النهائي
1 NUL bytes في bootstrap_js.h توليد بايثون + safety checks
2 توازن [ ] في JS strings استخدام \u005B \u005D
3 g_bytes_new undefined queue-based polling
4 this.onResume() recursion origOnResume.call(this)
5 Java.use classloader خاطئ enumerateClassLoadersSync
6 new ModView خارج UI thread runOnUiThread
7 Java.choose يجمّد كل 5 محاولات فقط
8 Pump loop مع default context get_thread_default أولاً
9 Regex يحذف أكثر من اللازم line-based insertion
10 Envelope JSON لـ Script::post تمرير كامل

---

القسم 11 — استكشاف الأخطاء

11.1 فشل البناء

الرسالة السبب الحل
undefined reference to yam_* رابط مفقود تحقق من symbol في libyamjs.a
undefined reference to g_* symbol غير موجود استخدم yam_* أو _yamfl_*
no member named X in namespace yam خطأ namespace تحقق من extern "C"
expected unqualified-id قوس ناقص أعد كتابة الكتلة
sizeof incomplete type مصفوفة كاملة أضف عنصر ناقص

11.2 فشل Runtime

اللوغ السبب الحل
bootstrap load failed: create JS syntax error فحص توازن + NUL
SyntaxError: unexpected token: 'cmd' Script::post خاطئ envelope كامل
ClassNotFoundException classloader خاطئ enumerateClassLoadersSync
attach_giveup لا activity pickBestActivity + Java.choose
no ModView classloader تحقق من resolveLoader
تجمّد UI pump loop محجوب استخدم sleep(2ms)

11.3 أدوات تشخيص

```bash
# GMainContext
grep "pump_once" logcat

# Script::post
grep "Script::post:" logcat

# TRAMPOLINE (JS→C)
grep "TRAMPOLINE CALLED" logcat

# Poller
grep "poller_" logcat

# ClassBrowser
grep "CB:" logcat
```

---

القسم 12 — سكريبتات جاهزة

12.1 استدعاء AppsFlyer logEvent

```javascript
(function () {
    send({type: "console", level: "log", line: "[TEST] start"});
    Java.performNow(function () {
        try {
            var AF = Java.use("com.appsflyer.AppsFlyerLib");
            var HM = Java.use("java.util.HashMap");
            var m = HM.$new();
            AF.logEvent.overload("java.lang.String", "java.util.HashMap")
                .call(AF.getInstance(), "af_level_10_completed", m);
            send({type: "console", level: "log", line: "[TEST] OK"});
        } catch (e) {
            send({type: "console", level: "error", line: "[TEST] " + e});
        }
    });
})();
```

12.2 تنصت على logEvent

```javascript
(function () {
    var AF = Java.use("com.appsflyer.AppsFlyerLib");
    var ovs = AF.logEvent.overloads;
    for (var i = 0; i < ovs.length; i++) {
        (function (idx) {
            var ov = ovs[idx];
            ov.implementation = function () {
                var a = Array.prototype.slice.call(arguments);
                var s = a.map(function (x) {
                    try { return x === null ? "null" : String(x); }
                    catch (e) { return "<err>"; }
                }).join(", ");
                send({type: "console", level: "log",
                      line: "[AF] logEvent(" + s + ")"});
                return ov.apply(this, a);
            };
        })(i);
    }
    send({type: "console", level: "log", line: "[AF] hooked " + ovs.length});
})();
```

---

القسم 13 — ملخص للالتزام

عند بداية أي رد، يجب على Claude:

1. قراءة السياق — معرفة الملفات المعنية
2. التحقق من التوقيعات قبل أي تعديل
3. كتابة كتل تنفيذية بالأوامر الصحيحة
4. إرفاق الأوامر الثلاثة في النهاية
5. عدم الاعتذار بلا سبب — فقط عند الخطأ الفعلي
6. عدم التبسيط — الكود كامل
7. عدم التأجيل — كل ما طُلب يُنجز
8. عدم استخدام /tmp في Termux
9. التحقق من توازن الأقواس و NUL bytes
10. النسخ الاحتياطي قبل الحذف

عند نهاية كل رد، يجب أن يحتوي:

☐ تشخيص موجز
☐ كتل مرقّمة
☐ الأوامر الثلاثة
☐ توقّع النتيجة

---

نهاية التوثيق

