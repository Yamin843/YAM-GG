// Auto-generated. Do not edit.
#ifndef YAMGG_BOOTSTRAP_JS_H
#define YAMGG_BOOTSTRAP_JS_H

namespace yamgg_bootstrap {

static const char kBootstrapSrc[] = R"YAMJS(
(function () {
    "use strict";

    // ═══════════════════════════════════════════════════════
    // HANDLE REGISTRY
    // ═══════════════════════════════════════════════════════
    var table = Object.create(null);
    var nextHandle = 1;
    function alloc(o) { var id = nextHandle++; table[id] = o; return id; }
    function get(id) { return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null; }
    function drop(id) { if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id]; }
    function dropAll() { table = Object.create(null); nextHandle = 1; }

    // ═══════════════════════════════════════════════════════
    // REPLY ENVELOPE
    // ═══════════════════════════════════════════════════════
    function reply(id, ok, kind, value, handle, error) {
        var m = { id: id, ok: !!ok };
        if (kind)   m.kind = kind;
        if (value  !== undefined && value  !== null) m.result = value;
        if (handle !== undefined && handle !== null) m.handle = handle;
        if (error)  m.error = String(error);
        try { send({ type: "reply", payload: JSON.stringify(m) }); } catch (e) {}
    }
    function replyHandle(id, h) { reply(id, true,  "handle", null, h); }
    function replyValue(id, v)  { reply(id, true,  "value",  v,    null); }
    function replyVoid(id)      { reply(id, true,  "void",   null, null); }
    function replyError(id, e)  { reply(id, false, "error", null, null, (e && e.message) ? e.message : String(e)); }

    // ═══════════════════════════════════════════════════════
    // JNI SIG PARSER
    // ═══════════════════════════════════════════════════════
    function prim(c) {
        switch (c) {
        case "Z": return "boolean"; case "B": return "byte";
        case "C": return "char";    case "S": return "short";
        case "I": return "int";     case "J": return "long";
        case "F": return "float";   case "D": return "double";
        case "V": return "void";    default:  return c;
        }
    }
    var LB = "\u005B";
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

    // ═══════════════════════════════════════════════════════
    // MATERIALIZER
    // ═══════════════════════════════════════════════════════
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
            case "null":    return null;
            case "undefined": return undefined;
            case "enum":    return Java.use(v.className).valueOf(v.enumName);
            case "array":   return Java.array(v.elementType, (v.elements || []).map(mat));
            default: return v.value;
            }
        }
        if ("value" in v) return v.value;
        return v;
    }

    // ═══════════════════════════════════════════════════════
    // COMMAND HANDLERS
    // ═══════════════════════════════════════════════════════
    var handlers = {
        cpp_use_class: function (cmd) { replyHandle(cmd.id, alloc(Java.use(cmd.className))); },
        cpp_get_method: function (cmd) {
            var c = get(cmd.classHandle); if (!c) throw new Error("no class handle");
            var m = c[cmd.methodName]; if (!m) throw new Error("no method: " + cmd.methodName);
            var method = cmd.signature ? m.overload.apply(m, parseSig(cmd.signature))
                                       : ((m.overloads && m.overloads.length > 0) ? m.overloads[0] : m);
            replyHandle(cmd.id, alloc(method));
        },
        cpp_get_field: function (cmd) {
            var cls = Java.use(cmd.className); var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            var isStatic = false;
            try { var af = cls.class.getDeclaredFields();
                  for (var j = 0; j < af.length; j++) {
                      if (String(af[j].getName()) === cmd.fieldName) {
                          isStatic = (Number(af[j].getModifiers()) & 8) !== 0; break; } } } catch (e) {}
            replyValue(cmd.id, JSON.stringify({ static: isStatic, handle: alloc(f) }));
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
        cpp_inspect_handle: function (cmd) {
            var obj = get(cmd.handleId); if (!obj) throw new Error("no handle");
            var fields = [];
            try { var cls = Java.use(obj.$className); var flds = cls.class.getDeclaredFields();
                  for (var i = 0; i < flds.length && i < 32; i++) {
                      var f = flds[i];
                      if ((Number(f.getModifiers()) & 8) !== 0) continue;
                      try { f.setAccessible(true); var fv = f.get(obj);
                            fields.push({ name: String(f.getName()), type: String(f.getType().getName()), value: (fv === null ? "null" : String(fv)) });
                      } catch (e) {} } } catch (e) {}
            replyValue(cmd.id, { className: obj.$className || "unknown", stringValue: String(obj), fields: fields });
        },
        cpp_noop: function (cmd) { replyVoid(cmd.id); }
    };

    // ═══════════════════════════════════════════════════════
    // RECEIVER — yamgg_cmd channel
    // ═══════════════════════════════════════════════════════
    recv("yamgg_cmd", function (msg) {
        var cmd = null;
        try { cmd = (typeof msg === "string") ? JSON.parse(msg) : msg; } catch (e) { return; }
        if (!cmd || !cmd.action) return;
        var h = handlers[cmd.action];
        if (!h) { replyError(cmd.id, new Error("unknown action: " + cmd.action)); return; }
        try { h(cmd); } catch (e) { replyError(cmd.id, e); }
    });

    // ═══════════════════════════════════════════════════════
    // LOG WRITER — use FileOutputStream (avoids FileWriter.write overload issue)
    // ═══════════════════════════════════════════════════════
    var LOG_PATH = "/storage/emulated/0/Download/appsflyer_calls.log";
    function writeLog(line) {
        try {
            var FOS = Java.use("java.io.FileOutputStream");
            var fos = FOS.$new(LOG_PATH, true);
            try {
                var String_ = Java.use("java.lang.String");
                var bytes = String_.$new(line + "\n").getBytes("UTF-8");
                fos.write(bytes);
                fos.flush();
            } finally { fos.close(); }
        } catch (e) {
            send({type:"appsflyer_log_error", message:"" + e});
        }
    }

    // ═══════════════════════════════════════════════════════
    // ATTACH — retry loop until activity found
    // ═══════════════════════════════════════════════════════
    var attachAttempts = 0;
    var attachDone = false;
    function tryAttach() {
        if (attachDone) return;
        attachAttempts++;
        if (attachAttempts > 120) {  // 60 seconds
            send({type:"attach_giveup", attempts: attachAttempts});
            return;
        }
        try {
            Java.performNow(function () {
                try {
                    Java.scheduleOnMainThread(function () {
                        try {
                            var AT = Java.use("android.app.ActivityThread");
                            var at = AT.currentActivityThread();
                            if (!at) { setTimeout(tryAttach, 500); return; }
                            var mActivities = at.mActivities.value;
                            if (!mActivities) { setTimeout(tryAttach, 500); return; }
                            var n = mActivities.size();
                            for (var i = 0; i < n; i++) {
                                try {
                                    var rec = mActivities.valueAt(i);
                                    if (!rec) continue;
                                    var act = null;
                                    try { act = rec.activity.value; } catch (e) { continue; }
                                    if (!act) continue;
                                    try { if (act.isFinishing()) continue; } catch (e) {}
                                    var ModView = Java.use("com.yamgg.modview.ModView");
                                    ModView.attach(act);
                                    attachDone = true;
                                    send({type:"attach_now_ok", className: "" + act.getClass().getName(), index: i, attempts: attachAttempts});
                                    return;
                                } catch (e) { send({type:"attach_now_skip", index: i, message: "" + e}); }
                            }
                            setTimeout(tryAttach, 500);
                        } catch (e) {
                            send({type:"attach_now_error", message: "" + e});
                            setTimeout(tryAttach, 500);
                        }
                    });
                } catch (e) {
                    send({type:"attach_now_outer", message: "" + e});
                    setTimeout(tryAttach, 500);
                }
            });
        } catch (e) {
            send({type:"attach_now_outer2", message: "" + e});
            setTimeout(tryAttach, 500);
        }
    }

    // ═══════════════════════════════════════════════════════
    // INSTALL HOOKS (run once)
    // ═══════════════════════════════════════════════════════
    function installAllHooks() {
        // onResume hook
        try {
            Java.performNow(function () {
                try {
                    var Activity = Java.use("android.app.Activity");
                    var origOnResume = Activity.onResume;
                    Activity.onResume.implementation = function () {
                        try { origOnResume.call(this); } catch (e) {}
                        try {
                            var ModView = Java.use("com.yamgg.modview.ModView");
                            ModView.attach(this);
                            send({type:"modview_attached", className: "" + this.getClass().getName()});
                        } catch (e) { send({type:"attach_error", message: "" + e}); }
                    };
                    send({type:"hook_installed"});
                } catch (e) { send({type:"hook_error", message: "install: " + e}); }
            });
        } catch (e) { send({type:"hook_outer_error", message: "" + e}); }

        // AppsFlyer hooks
        try {
            Java.performNow(function () {
                try {
                    var C = Java.use("com.appsflyer.unity.AppsFlyerAndroidWrapper");
                    send({type:"appsflyer_class_ok"});

                    try {
                        var m2 = C.trackEvent.overload("java.lang.String", "java.util.HashMap");
                        m2.implementation = function (name, params) {
                            writeLog("" + new Date() + "  trackEvent/2  name=" + name);
                            send({type:"appsflyer_hit", overload:"2", name:"" + name});
                            return m2.call(this, name, params);
                        };
                        send({type:"appsflyer_hook_ok", overload:"2"});
                    } catch (e) { send({type:"appsflyer_hook_err", overload:"2", message:"" + e}); }

                    try {
                        var m4 = C.trackEvent.overload("java.lang.String", "java.util.HashMap", "boolean", "java.lang.String");
                        m4.implementation = function (name, params, isRevenue, currency) {
                            writeLog("" + new Date() + "  trackEvent/4  name=" + name + "  isRevenue=" + isRevenue);
                            send({type:"appsflyer_hit", overload:"4", name:"" + name});
                            return m4.call(this, name, params, isRevenue, currency);
                        };
                        send({type:"appsflyer_hook_ok", overload:"4"});
                    } catch (e) { send({type:"appsflyer_hook_err", overload:"4", message:"" + e}); }

                    send({type:"appsflyer_ready"});
                } catch (e) { send({type:"appsflyer_class_error", message:"" + e}); }
            });
        } catch (e) { send({type:"appsflyer_outer_error", message:"" + e}); }
    }

    // ═══════════════════════════════════════════════════════
    // READY + SCHEDULE
    // ═══════════════════════════════════════════════════════
    try { send({ type: "cpp_ready" }); }       catch (e) {}
    try { send({ type: "cpp_ready_final" }); } catch (e) {}

    setTimeout(function () {
        try { installAllHooks(); } catch (e) { send({type:"install_err", message:"" + e}); }
        try { tryAttach(); } catch (e) { send({type:"attach_start_err", message:"" + e}); }
    }, 800);
})();
)YAMJS";

} // namespace yamgg_bootstrap

#endif // YAMGG_BOOTSTRAP_JS_H
