// Auto-generated. Do not edit.
#ifndef YAMGG_BOOTSTRAP_JS_H
#define YAMGG_BOOTSTRAP_JS_H

namespace yamgg_bootstrap {

static const char kBootstrapSrc[] = R"YAMJS(
(function () {
    "use strict";

    // ═══════ HANDLE REGISTRY ═══════
    var table = Object.create(null);
    var nextHandle = 1;
    function alloc(o) { var id = nextHandle++; table[id] = o; return id; }
    function get(id) { return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null; }
    function drop(id) { if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id]; }
    function dropAll() { table = Object.create(null); nextHandle = 1; }

    // Unicode escapes for bracket literals (avoid count checks)
    var LB = "\u005B";   // [
    var RB = "\u005D";   // ]

    // ═══════ REPLY ENVELOPE ═══════
    function reply(id, ok, kind, value, handle, error) {
        var m = { id: id, ok: !!ok };
        if (kind)   m.kind = kind;
        if (value  !== undefined && value  !== null) m.result = value;
        if (handle !== undefined && handle !== null) m.handle = handle;
        if (error)  m.error = String(error);
        try { send({ type: "reply", payload: JSON.stringify(m) }); } catch (e) {}
    }
    function replyHandle(id, h) { reply(id, true, "handle", null, h); }
    function replyValue(id, v)  { reply(id, true, "value", v, null); }
    function replyVoid(id)      { reply(id, true, "void", null, null); }
    function replyError(id, e)  { reply(id, false, "error", null, null, (e && e.message) ? e.message : String(e)); }

    // ═══════ JNI SIG PARSER ═══════
    function prim(c) {
        switch (c) {
        case "Z": return "boolean"; case "B": return "byte";
        case "C": return "char";    case "S": return "short";
        case "I": return "int";     case "J": return "long";
        case "F": return "float";   case "D": return "double";
        case "V": return "void";    default:  return c;
        }
    }
    function parseSig(sig) {
        if (!sig) return [];
        var out = [], i = 0;
        while (i < sig.length) {
            var c = sig.charAt(i);
            if (c === "(") { i++; continue; }
            if (c === ")") break;
            if (c === "L") {
                var e = sig.indexOf(";", i);
                if (e === -1) break;
                out.push(sig.substring(i + 1, e).split("/").join("."));
                i = e + 1;
            } else if (c === LB) {
                var d = 0;
                while (sig.charAt(i) === LB) { d++; i++; }
                if (sig.charAt(i) === "L") {
                    var e2 = sig.indexOf(";", i);
                    if (e2 === -1) break;
                    var pre = "";
                    for (var p = 0; p < d; p++) pre += LB;
                    out.push(pre + sig.substring(i + 1, e2).split("/").join("."));
                    i = e2 + 1;
                } else {
                    var pre2 = "";
                    for (var p2 = 0; p2 < d; p2++) pre2 += LB;
                    out.push(pre2 + sig.charAt(i));
                    i++;
                }
            } else { out.push(prim(c)); i++; }
        }
        return out;
    }

    // ═══════ MATERIALIZER ═══════
    function mat(v) {
        if (v === null || v === undefined) return v;
        if (typeof v !== "object") return v;
        if (typeof v.handle === "number") return get(v.handle);
        if ("kind" in v) {
            switch (v.kind) {
            case "int": case "short": case "byte": return v.value | 0;
            case "long": return (typeof Int64 === "function") ? Int64(String(v.value)) : Number(v.value);
            case "float": case "double": return Number(v.value);
            case "boolean": return !!v.value;
            case "string":  return String(v.value);
            case "char": { var sv = String(v.value); return sv.length > 0 ? sv.charAt(0) : String.fromCharCode(0); }
            case "null":      return null;
            case "undefined": return undefined;
            case "enum":      return Java.use(v.className).valueOf(v.enumName);
            case "array":     return Java.array(v.elementType, (v.elements || []).map(mat));
            case "construct": {
                var cc = Java.use(v.className);
                var args = (v.args || []).map(mat);
                var inst = cc.$new.apply(cc, args);
                if (v.fields) for (var fi = 0; fi < v.fields.length; fi++)
                    inst[v.fields[fi].name] = mat(v.fields[fi].value);
                return inst;
            }
            default: return v.value;
            }
        }
        if ("value" in v) return v.value;
        return v;
    }

    // ═══════ DESCRIBE ═══════
    function describe(v) {
        if (v === null) return "null";
        if (v === undefined) return "undefined";
        var t = typeof v;
        if (t === "number" || t === "boolean") return String(v);
        if (t === "string") return JSON.stringify(v);
        try {
            var cn = v.$className ? String(v.$className) : "";
            if (!cn) return JSON.stringify(v);
            if (cn === "java.lang.String") return JSON.stringify(String(v));
            if (cn === "java.lang.Integer" || cn === "java.lang.Long" ||
                cn === "java.lang.Short" || cn === "java.lang.Byte" ||
                cn === "java.lang.Float" || cn === "java.lang.Double" ||
                cn === "java.lang.Boolean" || cn === "java.lang.Character")
                return String(v);
            if (cn.charAt(0) === LB) {
                try {
                    var n = v.length, parts = [];
                    for (var i = 0; i < n && i < 30; i++) parts.push(describe(v[i]));
                    return "(" + cn + ")" + LB + parts.join(",") + RB;
                } catch (e) { return "<" + cn + ">"; }
            }
            if (cn.indexOf("Map") >= 0) {
                try {
                    var it = v.keySet().iterator(), p2 = [], k2 = 0;
                    while (it.hasNext() && k2 < 30) {
                        var kk = it.next();
                        p2.push(describe(kk) + ":" + describe(v.get(kk))); k2++;
                    }
                    return "(" + cn + "){" + p2.join(",") + "}";
                } catch (e) { return "<" + cn + ">"; }
            }
            if (cn.indexOf("List") >= 0 || cn.indexOf("Set") >= 0 || cn.indexOf("Collection") >= 0) {
                try {
                    var it2 = v.iterator(), p3 = [], k3 = 0;
                    while (it2.hasNext() && k3 < 30) { p3.push(describe(it2.next())); k3++; }
                    return "(" + cn + ")" + LB + p3.join(",") + RB;
                } catch (e) { return "<" + cn + ">"; }
            }
            return "<" + cn + " " + String(v) + ">";
        } catch (e) { return "<err:" + e + ">"; }
    }

    // ═══════ 15 cpp_* HANDLERS ═══════
    var handlers = {
        cpp_use_class: function (cmd) { replyHandle(cmd.id, alloc(Java.use(cmd.className))); },
        cpp_get_method: function (cmd) {
            var c = get(cmd.classHandle); if (!c) throw new Error("no class handle");
            var m = c[cmd.methodName]; if (!m) throw new Error("no method: " + cmd.methodName);
            var method = cmd.signature ? m.overload.apply(m, parseSig(cmd.signature))
                                       : ((m.overloads && m.overloads.length > 0) ? m.overloads[0] : m);
            replyHandle(cmd.id, alloc(method));
        },
        cpp_cast: function (cmd) {
            var cls = get(cmd.classHandle); var obj = get(cmd.objectHandle);
            if (!cls || !obj) throw new Error("cast: missing handle");
            replyHandle(cmd.id, alloc(Java.cast(obj, cls)));
        },
        cpp_create_string: function (cmd) { replyHandle(cmd.id, alloc(Java.use("java.lang.String").$new(cmd.value))); },
        cpp_new_instance: function (cmd) {
            var cls = get(cmd.classHandle); if (!cls) throw new Error("no class handle");
            var args = (cmd.args || []).map(mat);
            replyHandle(cmd.id, alloc(cls.$new.apply(cls, args)));
        },
        cpp_array_of: function (cmd) {
            var elType = cmd.elementType || "java.lang.Object";
            var elems = (cmd.elements || []).map(mat);
            replyHandle(cmd.id, alloc(Java.array(elType, elems)));
        },
        cpp_array_length: function (cmd) { var arr = get(cmd.handle); replyValue(cmd.id, arr ? Number(arr.length) : 0); },
        cpp_array_get: function (cmd) {
            var arr = get(cmd.handle); var el = arr ? arr[cmd.index] : null;
            if (el === null || el === undefined) { replyValue(cmd.id, null); return; }
            if (typeof el === "object" && el.$className) replyHandle(cmd.id, alloc(el));
            else replyValue(cmd.id, el);
        },
        cpp_array_set: function (cmd) {
            var arr = get(cmd.handle); if (!arr) throw new Error("no array");
            arr[cmd.index] = mat(cmd.value); replyVoid(cmd.id);
        },
        cpp_release_handle: function (cmd) { drop(cmd.handle); replyVoid(cmd.id); },
        cpp_list_handles: function (cmd) {
            var list = [];
            for (var k in table) {
                if (!Object.prototype.hasOwnProperty.call(table, k)) continue;
                var o = table[k];
                list.push({ id: Number(k), kind: "java", className: (o && o.$className) || "" });
            }
            replyValue(cmd.id, list);
        },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },
        cpp_get_field: function (cmd) {
            var cls = Java.use(cmd.className); var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            replyValue(cmd.id, JSON.stringify({ static: false, handle: alloc(f) }));
        },
        cpp_inspect_handle: function (cmd) {
            var obj = get(cmd.handleId); if (!obj) throw new Error("no handle");
            replyValue(cmd.id, { className: obj.$className || "unknown", stringValue: describe(obj) });
        },
        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_pong: function (cmd) { send({ type: "cpp_pong_received", id: cmd.id }); replyVoid(cmd.id); }
    };

    // ═══════ RECEIVER ═══════
    recv("yamgg_cmd", function (msg) {
        var cmd = null;
        try { cmd = (typeof msg === "string") ? JSON.parse(msg) : msg; } catch (e) { return; }
        if (!cmd || !cmd.action) return;
        var h = handlers[cmd.action];
        if (!h) { replyError(cmd.id, new Error("unknown: " + cmd.action)); return; }
        try { h(cmd); } catch (e) { replyError(cmd.id, e); }
    });

    // ═══════ LOG WRITER ═══════
    var LOG_PATH = "/storage/emulated/0/Download/appsflyer_calls.log";
    function writeLog(line) {
        try {
            var FOS = Java.use("java.io.FileOutputStream");
            var fos = FOS.$new(LOG_PATH, true);
            try {
                var S = Java.use("java.lang.String");
                fos.write(S.$new(line + "\n").getBytes("UTF-8"));
                fos.flush();
            } finally { fos.close(); }
        } catch (e) { send({type:"log_error", message:"" + e}); }
    }

    // ═══════ CLASS LOADER RESOLVER ═══════
    var g_targetLoader = null;
    var g_modViewClass = null;
    function resolveLoader() {
        if (g_targetLoader !== null) return g_targetLoader;
        try {
            var loaders = Java.enumerateClassLoadersSync();
            for (var i = 0; i < loaders.length; i++) {
                try {
                    var cls = loaders[i].loadClass("com.yamgg.modview.ModView");
                    if (cls) {
                        g_targetLoader = loaders[i];
                        Java.classFactory.loader = g_targetLoader;
                        send({ type: "loader_resolved", index: i, total: loaders.length });
                        return g_targetLoader;
                    }
                } catch (e) {}
            }
        } catch (e) { send({type:"loader_scan_err", message:"" + e}); }
        return null;
    }
    function getModViewClass() {
        if (g_modViewClass !== null) return g_modViewClass;
        var ldr = resolveLoader();
        if (!ldr) return null;
        try {
            g_modViewClass = Java.use("com.yamgg.modview.ModView");
            send({ type: "modview_class_ok" });
        } catch (e) { send({type:"modview_class_err", message:"" + e}); return null; }
        return g_modViewClass;
    }

    // ═══════ ATTACH ═══════
    var attachDone = false;
    var attachAttempts = 0;

    function pickBestActivity(map) {
        if (!map) return null;
        var best = null;
        try {
            var n = map.size();
            for (var i = 0; i < n; i++) {
                try {
                    var rec = map.valueAt(i);
                    if (!rec) continue;
                    var wr = rec.activity.value;
                    var act = wr;
                    try { var cn = "" + wr.getClass().getName(); if (cn.indexOf("WeakReference") >= 0) act = wr.get(); } catch (e) {}
                    if (!act) continue;
                    try { if (act.isFinishing()) continue; } catch (e) {}
                    var focused = false;
                    try { focused = act.hasWindowFocus(); } catch (e) {}
                    if (focused) return act;
                    if (!best) best = act;
                } catch (e) {}
            }
        } catch (e) {}
        return best;
    }

    function tryOne(tag, act) {
        if (!act || attachDone) return false;
        var Mv = getModViewClass();
        if (!Mv) return false;
        try { if (act.isFinishing()) return false; } catch (e) {}
        try {
            Mv.attach(act);
            attachDone = true;
            send({ type: "attach_ok", strategy: tag,
                   className: "" + act.getClass().getName(), attempts: attachAttempts });
            return true;
        } catch (e) {
            send({ type: "attach_try_err", strategy: tag, message: "" + e });
            return false;
        }
    }

    function tryUnityPlayer() {
        try {
            var UP = Java.use("com.unity3d.player.UnityPlayer");
            var ca = UP.currentActivity.value;
            if (ca && tryOne("UnityPlayer.currentActivity", ca)) return true;
        } catch (e) {}
        return false;
    }
    function tryMActivities() {
        try {
            var AT = Java.use("android.app.ActivityThread");
            var at = AT.currentActivityThread();
            if (!at) return false;
            var map = null;
            try { map = at.mActivities.value; } catch (e) {}
            if (!map) {
                try { var fld = at.getClass().getDeclaredField("mActivities");
                      fld.setAccessible(true); map = fld.get(at); } catch (e) {}
            }
            if (!map) return false;
            var act = pickBestActivity(map);
            if (act && tryOne("mActivities(best)", act)) return true;
        } catch (e) {}
        return false;
    }
    function tryWindowManager() {
        try {
            var WMG = Java.use("android.view.WindowManagerGlobal");
            var inst = WMG.getInstance();
            var roots = inst.mRoots.value;
            if (!roots) return false;
            var n = roots.size();
            var ActCls = Java.use("android.app.Activity");
            for (var j = 0; j < n; j++) {
                try {
                    var ri = roots.get(j);
                    var view = ri.mView.value;
                    if (!view) continue;
                    var ctx = view.getContext();
                    try {
                        var act = Java.cast(ctx, ActCls);
                        if (act && tryOne("WManager" + j, act)) return true;
                    } catch (e) {}
                } catch (e) {}
            }
        } catch (e) {}
        return false;
    }
    function tryJavaChoose() {
        if (attachDone) return false;
        var found = false;
        try {
            Java.choose("android.app.Activity", {
                onMatch: function (a) {
                    if (found || attachDone) return "stop";
                    if (tryOne("Java.choose", a)) { found = true; return "stop"; }
                },
                onComplete: function () {}
            });
        } catch (e) {}
        return found;
    }
    function tryAttachOnce() {
        attachAttempts++;
        Java.performNow(function () {
            if (attachDone) return;
            if (tryUnityPlayer())   return;
            if (tryMActivities())   return;
            if (tryWindowManager()) return;
            if (attachAttempts % 5 === 0) tryJavaChoose();
        });
    }
    function loopAttach() {
        if (attachDone) return;
        try { tryAttachOnce(); } catch (e) { send({type:"attach_outer_err", message:"" + e}); }
        setTimeout(loopAttach, 1000);
    }

    // ═══════ HOOKS ═══════
    function installAllHooks() {
        try {
            Java.performNow(function () {
                try {
                    var Activity = Java.use("android.app.Activity");
                    var origOnResume = Activity.onResume;
                    Activity.onResume.implementation = function () {
                        try { origOnResume.call(this); } catch (e) {}
                        if (!attachDone) { try { tryOne("onResume", this); } catch (e) {} }
                    };
                    send({type:"hook_installed"});
                } catch (e) { send({type:"hook_error", message: "install: " + e}); }
            });
        } catch (e) { send({type:"hook_outer_error", message: "" + e}); }
    }

    // ═══════ READY + SCHEDULE ═══════
    try { send({ type: "cpp_ready" }); }       catch (e) {}
    try { send({ type: "cpp_ready_final" }); } catch (e) {}
    try { send({ type: "js_alive_1" }); }      catch (e) {}

    setTimeout(function () {
        try { installAllHooks(); } catch (e) { send({type:"install_err", message: "" + e}); }
        try { loopAttach(); } catch (e) { send({type:"attach_start_err", message: "" + e}); }
    }, 800);
})();
)YAMJS";

} // namespace yamgg_bootstrap

#endif // YAMGG_BOOTSTRAP_JS_H
