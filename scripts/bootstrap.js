// ===========================================================================
// YAM-GG bootstrap.js — poller-driven, no setTimeout
// ===========================================================================
(function () {
    "use strict";

    try { send({type:"boot_top_v5"}); } catch (e) {}

    // ─── Handle registry ──────────────────────────────────────────────
    var table = Object.create(null);
    var nextHandle = 1;
    function alloc(o) {
        var id = nextHandle++;
        table[id] = o;
        return id;
    }
    function get(id) {
        return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null;
    }
    function drop(id) {
        if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id];
    }
    function dropAll() { table = Object.create(null); nextHandle = 1; }

    var LB = "\u005B";
    var RB = "\u005D";

    // ─── Reply envelope ───────────────────────────────────────────────
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
    function replyError(id, e)  {
        reply(id, false, "error", null, null,
              (e && e.message) ? e.message : String(e));
    }

    // ─── JNI sig parser ───────────────────────────────────────────────
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

    // ─── JSON → Java materializer ─────────────────────────────────────
    function mat(v) {
        if (v === null || v === undefined) return v;
        if (typeof v !== "object") return v;
        if (typeof v.handle === "number") return get(v.handle);
        if (!("kind" in v)) {
            if ("value" in v) return v.value;
            return v;
        }
        switch (v.kind) {
        case "int": case "short": case "byte": return v.value | 0;
        case "long":
            if (typeof Int64 === "function") return Int64(String(v.value));
            return Number(v.value);
        case "float": case "double": return Number(v.value);
        case "boolean": return !!v.value;
        case "string":  return String(v.value);
        case "char": {
            var sv = String(v.value);
            return sv.length > 0 ? sv.charAt(0) : String.fromCharCode(0);
        }
        case "null":      return null;
        case "undefined": return undefined;
        case "enum":      return Java.use(v.className).valueOf(v.enumName);
        case "array": {
            var elType = v.elementType || "java.lang.Object";
            return Java.array(elType, (v.elements || []).map(mat));
        }
        case "construct": {
            var cc = Java.use(v.className);
            var cargs = (v.args || []).map(mat);
            var inst;
            if (v.ctorSig) {
                inst = cc.$alloc();
                cc.$init.overload.apply(cc.$init, parseSig(v.ctorSig))
                    .call(inst, ...cargs);
            } else {
                inst = cc.$new.apply(cc, cargs);
            }
            return inst;
        }
        case "expr": {
            try {
                var fn = new Function("Java", "return (" + v.expr + ");");
                return fn(Java);
            } catch (e) { return null; }
        }
        default: return v.value;
        }
    }

    // ─── describe ─────────────────────────────────────────────────────
    function describe(v) { return describeRec(v, new WeakSet(), 0); }
    function describeRec(v, seen, depth) {
        if (v === null) return "null";
        if (v === undefined) return "undefined";
        var t = typeof v;
        if (t === "number" || t === "boolean") return String(v);
        if (t === "string") return JSON.stringify(v);
        if (t === "function") return "<fn " + (v.name || "anon") + ">";
        if (t !== "object") return String(v);
        if (typeof seen.add === "function") {
            if (seen.has(v)) return "<circular>";
            try { seen.add(v); } catch (e) {}
        }
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
                var out = [], n = 0;
                try { n = Number(v.length) || 0; } catch (e) { n = 0; }
                for (var i = 0; i < n; i++) {
                    var el;
                    try { el = v[i]; } catch (e) { el = null; }
                    out.push(describeRec(el, seen, depth + 1));
                }
                return "(" + cn + ")" + LB + out.join(",") + RB;
            }
            if (cn.indexOf("Map") >= 0) {
                var mp = [];
                try {
                    var it = v.entrySet().iterator();
                    while (it.hasNext()) {
                        var e = it.next();
                        mp.push(describeRec(e.getKey(), seen, depth + 1) + ":" +
                                describeRec(e.getValue(), seen, depth + 1));
                    }
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + "){" + mp.join(",") + "}";
            }
            if (cn.indexOf("List") >= 0 || cn.indexOf("Set") >= 0 ||
                cn.indexOf("Collection") >= 0) {
                var li = [];
                try {
                    var it2 = v.iterator();
                    while (it2.hasNext())
                        li.push(describeRec(it2.next(), seen, depth + 1));
                } catch (e) { return "<" + cn + ">"; }
                return "(" + cn + ")" + LB + li.join(",") + RB;
            }
            return "<" + cn + " " + String(v) + ">";
        } catch (e) { return "<err:" + e + ">"; }
    }

    // ─── Chunked sender ───────────────────────────────────────────────
    var _nextChunkSession = 1;
    var CHUNK_THRESHOLD = 128 * 1024;
    var CHUNK_SIZE      = 128 * 1024;

    function sendMaybeChunked(obj) {
        var json;
        try { json = JSON.stringify(obj); } catch (e) { json = ""; }
        if (json.length <= CHUNK_THRESHOLD) {
            try { send(obj); } catch (e) {}
            return;
        }
        var kind = obj && obj.type ? String(obj.type) : "chunked";
        var sid = _nextChunkSession++;
        var total = Math.max(1, Math.ceil(json.length / CHUNK_SIZE));
        try {
            send({type:"chunk_begin", sessionId:sid, kind:kind,
                  totalChunks:total, totalBytes:json.length});
        } catch (e) {}
        for (var i = 0; i < total; i++) {
            try {
                send({type:"chunk_data", sessionId:sid, index:i,
                      totalChunks:total,
                      data:json.substring(i*CHUNK_SIZE, (i+1)*CHUNK_SIZE)});
            } catch (e) {}
        }
        try { send({type:"chunk_end", sessionId:sid, kind:kind}); } catch (e) {}
    }

    // ═══════════════════════════════════════════════════════════════
    //  cpp_* handlers (reply-based)
    // ═══════════════════════════════════════════════════════════════
    var cppHandlers = {
        cpp_use_class: function (cmd) {
            replyHandle(cmd.id, alloc(Java.use(cmd.className)));
        },
        cpp_get_method: function (cmd) {
            var c = get(cmd.classHandle);
            if (!c) throw new Error("no class handle");
            var m = c[cmd.methodName];
            if (!m) throw new Error("no method: " + cmd.methodName);
            var method = cmd.signature
                ? m.overload.apply(m, parseSig(cmd.signature))
                : ((m.overloads && m.overloads.length > 0)
                    ? m.overloads[0] : m);
            replyHandle(cmd.id, alloc(method));
        },
        cpp_release_handle: function (cmd) { drop(cmd.handle); replyVoid(cmd.id); },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },
        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_eval: function (cmd) {
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                reply(cmd.id, true, "value", describe(fn()));
            } catch (e) { replyError(cmd.id, e); }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  jv_* handlers (JV tab, event-based)
    // ═══════════════════════════════════════════════════════════════
    var jvHandlers = {
        jv_enumerate: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                var f = cmd.filter || "";
                var out = [];
                for (var i = 0; i < all.length; i++) {
                    if (f && all[i].indexOf(f) < 0) continue;
                    out.push(all[i]);
                }
                sendMaybeChunked({type:"classes_list", count:out.length,
                                  filter:f, classes:out});
            } catch (e) {
                send({type:"classes_list", count:0, filter:cmd.filter||"",
                      classes:[], error:""+e});
            }
        },
        jv_detail: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var methods = [];
                var ms = C.class.getDeclaredMethods();
                for (var i = 0; i < ms.length; i++) {
                    try {
                        var m = ms[i];
                        m.setAccessible(true);
                        var pts = m.getParameterTypes();
                        var args = [];
                        for (var j = 0; j < pts.length; j++) {
                            var tn = String(pts[j].getName());
                            var kd = "object";
                            var ev = [];
                            if (tn === "int") kd = "int";
                            else if (tn === "long") kd = "long";
                            else if (tn === "short") kd = "short";
                            else if (tn === "byte") kd = "byte";
                            else if (tn === "float") kd = "float";
                            else if (tn === "double") kd = "double";
                            else if (tn === "boolean") kd = "boolean";
                            else if (tn === "char") kd = "char";
                            else if (tn === "java.lang.String") kd = "string";
                            else if (tn.charAt(0) === LB) kd = "array";
                            else if (tn.indexOf("Map") >= 0) kd = "map";
                            else {
                                try {
                                    var pc = Java.use(tn);
                                    if (pc.class.isEnum()) {
                                        kd = "enum";
                                        var vals = pc.values();
                                        for (var e2 = 0; e2 < vals.length; e2++)
                                            ev.push(String(vals[e2].name()));
                                    }
                                } catch (ex) {}
                            }
                            args.push({typeName: tn, kind: kd, enumValues: ev});
                        }
                        var mods = m.getModifiers();
                        methods.push({
                            name: String(m.getName()),
                            ret: String(m.getReturnType().getName()),
                            isStatic: (mods & 8) !== 0,
                            args: args
                        });
                    } catch (e) {}
                }
                var fields = [];
                var fs = C.class.getDeclaredFields();
                for (var k = 0; k < fs.length; k++) {
                    try {
                        var f = fs[k];
                        f.setAccessible(true);
                        fields.push({
                            name: String(f.getName()),
                            type: String(f.getType().getName()),
                            isStatic: (f.getModifiers() & 8) !== 0,
                            value: ""
                        });
                    } catch (e) {}
                }
                sendMaybeChunked({type:"class_detail_result",
                                  className:cmd.className,
                                  methods:methods, fields:fields});
            } catch (e) {
                send({type:"class_detail_result", className:cmd.className,
                      methods:[], fields:[], error:""+e});
            }
        },
        jv_trace: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var n = 0;
                for (var i = 0; i < MM.overloads.length; i++) {
                    (function(ov){
                        if (ov.__ygg_trace !== undefined) return;
                        ov.__ygg_trace = ov.implementation || null;
                        ov.implementation = function() {
                            var a = Array.prototype.slice.call(arguments);
                            var s = [];
                            for (var k = 0; k < a.length; k++) {
                                try { s.push(describe(a[k])); }
                                catch (e) { s.push("<err>"); }
                            }
                            try {
                                send({type:"trace_line", level:"log",
                                      line:"[" + cmd.className + "." +
                                           cmd.methodName + "] (" +
                                           s.join(", ") + ")"});
                            } catch (e) {}
                            return ov.__ygg_trace
                                ? ov.__ygg_trace.apply(this, a) : null;
                        };
                        n++;
                    })(MM.overloads[i]);
                }
                send({type:"trace_ok", className:cmd.className,
                      methodName:cmd.methodName, count:n});
            } catch (e) {
                send({type:"trace_err", error:""+e});
            }
        },
        jv_untrace: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (MM && MM.overloads) {
                    for (var i = 0; i < MM.overloads.length; i++) {
                        var ov = MM.overloads[i];
                        if (ov.__ygg_trace !== undefined) {
                            ov.implementation = ov.__ygg_trace;
                            delete ov.__ygg_trace;
                        }
                    }
                }
                send({type:"untrace_ok"});
            } catch (e) { send({type:"untrace_err", error:""+e}); }
        },
        jv_instances: function (cmd) {
            try {
                var items = [];
                var C = Java.use(cmd.className);
                Java.choose(cmd.className, {
                    onMatch: function(x) {
                        if (items.length >= 20) return "stop";
                        try {
                            var h = alloc(x);
                            var flds = C.class.getDeclaredFields();
                            var fields = [];
                            for (var i = 0; i < flds.length; i++) {
                                var f = flds[i];
                                if ((f.getModifiers() & 8) !== 0) continue;
                                try {
                                    f.setAccessible(true);
                                    fields.push({
                                        name: String(f.getName()),
                                        value: describe(f.get(x))
                                    });
                                } catch (e) {}
                            }
                            items.push({handle:h,
                                        className:String(x.$className),
                                        fields:fields});
                        } catch (e) {}
                    },
                    onComplete: function() {}
                });
                send({type:"instances_result", className:cmd.className,
                      items:items});
            } catch (e) {
                send({type:"instances_result", className:cmd.className,
                      items:[], error:""+e});
            }
        },
        jv_call: function (cmd) {
            try {
                var C = Java.use(cmd.className);
                var MM = C[cmd.methodName];
                if (!MM || !MM.overloads) throw new Error("no overloads");
                var args = (cmd.argsSpec || []).map(mat);
                var isStatic = cmd.isStatic === true;
                var inst = null;
                if (!isStatic) {
                    if (cmd.instanceHandle > 0) inst = get(cmd.instanceHandle);
                    else Java.choose(cmd.className, {
                        onMatch: function(x){ inst = x; return "stop"; },
                        onComplete: function() {}
                    });
                    if (!inst) throw new Error("no instance");
                }
                var chosen = MM.overloads[0];
                for (var i = 0; i < MM.overloads.length; i++) {
                    if (MM.overloads[i].argumentTypes.length === args.length) {
                        chosen = MM.overloads[i];
                        break;
                    }
                }
                var r = isStatic ? chosen.apply(null, args) : chosen.apply(inst, args);
                send({type:"call_result", ok:true, result:describe(r)});
            } catch (e) {
                send({type:"call_result", ok:false, error:""+e});
            }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  native handlers
    // ═══════════════════════════════════════════════════════════════
    var nativeHandlers = {
        eval: function (cmd) {
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                send({type:"eval_result", ok:true, result:describe(fn())});
            } catch (e) {
                send({type:"eval_result", ok:false, error:""+e});
            }
        },
        run_user_script: function (cmd) {
            var nm = cmd.name || "anon";
            var code = cmd.code || "";
            try {
                Java.performNow(function () { (new Function(code))(); });
                send({type:"user_script_loaded", name:nm,
                      size:code.length, ok:true});
            } catch (e) {
                send({type:"user_script_loaded", name:nm,
                      ok:false, error:""+e});
            }
        },
        enumerate_classes: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                sendMaybeChunked({type:"classes_list", count:all.length, classes:all});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[], error:""+e});
            }
        }
    };

    // ─── Dispatcher ───────────────────────────────────────────────────
    function dispatchCmd(cmd) {
        if (!cmd || !cmd.action) return;
        var h = cppHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { replyError(cmd.id, e); } return; }
        h = jvHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { send({type:"jv_error", error:""+e}); } return; }
        h = nativeHandlers[cmd.action];
        if (h) { try { h(cmd); } catch (e) { send({type:"native_error", error:""+e}); } return; }
        if (cmd.action === "yamgg_tick" || cmd.action === "yamgg_boot") return;
        try { replyError(cmd.id, new Error("unknown: " + cmd.action)); } catch (e) {}
    }
    function unwrapCmd(msg) {
        if (!msg) return null;
        if (typeof msg === "string") {
            try { return JSON.parse(msg); } catch (e) { return null; }
        }
        if (msg.payload !== undefined && msg.payload !== null) return msg.payload;
        if (msg.action) return msg;
        return null;
    }

    try {
        recv("yamgg_cmd", function (msg) { dispatchCmd(unwrapCmd(msg)); });
    } catch (e) {}

    // ═══════════════════════════════════════════════════════════════
    //  ModView loader resolution
    // ═══════════════════════════════════════════════════════════════
    var g_targetLoader = null;
    var g_modViewClass = null;

    function resolveLoader() {
        if (g_targetLoader !== null) return g_targetLoader;
        try {
            var loaders = Java.enumerateClassLoadersSync();
            for (var i = 0; i < loaders.length; i++) {
                try {
                    var cn = String(loaders[i].getClass().getName());
                    if (cn.indexOf("InMemoryDexClassLoader") < 0 &&
                        cn.indexOf("DexClassLoader") < 0 &&
                        cn.indexOf("PathClassLoader") < 0) continue;
                    var cls = loaders[i].loadClass("com.yamgg.modview.ModView");
                    if (cls) {
                        g_targetLoader = loaders[i];
                        Java.classFactory.loader = g_targetLoader;
                        send({type:"loader_resolved", index:i, total:loaders.length});
                        return g_targetLoader;
                    }
                } catch (e) {}
            }
        } catch (e) { send({type:"loader_scan_err", message:""+e}); }
        return null;
    }

    function getModViewClass() {
        if (g_modViewClass !== null) return g_modViewClass;
        var ldr = resolveLoader();
        if (!ldr) return null;
        try {
            g_modViewClass = Java.use("com.yamgg.modview.ModView");
            send({type:"modview_class_ok"});
        } catch (e) {
            send({type:"modview_class_err", message:""+e});
            return null;
        }
        return g_modViewClass;
    }

    // ═══════════════════════════════════════════════════════════════
    //  Attach to Activity
    // ═══════════════════════════════════════════════════════════════
    var attachDone = false;
    var attachAttempts = 0;

    function tryOne(tag, act) {
        if (!act || attachDone) return false;
        var Mv = getModViewClass();
        if (!Mv) return false;
        try { if (act.isFinishing()) return false; } catch (e) {}
        try {
            Mv.attach(act);
            attachDone = true;
            send({type:"attach_ok", strategy:tag,
                  className:""+act.getClass().getName(),
                  attempts:attachAttempts});
            return true;
        } catch (e) {
            send({type:"attach_try_err", strategy:tag, message:""+e});
            return false;
        }
    }

    function tryUnityPlayer() {
        try {
            var UP = Java.use("com.unity3d.player.UnityPlayer");
            var ca = UP.currentActivity.value;
            if (ca && tryOne("UnityPlayer", ca)) return true;
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
                try {
                    var fld = at.getClass().getDeclaredField("mActivities");
                    fld.setAccessible(true);
                    map = fld.get(at);
                } catch (e) {}
            }
            if (!map) return false;
            var n = map.size();
            for (var i = 0; i < n; i++) {
                try {
                    var rec = map.valueAt(i);
                    if (!rec) continue;
                    var wr = rec.activity.value;
                    var act = wr;
                    try {
                        var cn = "" + wr.getClass().getName();
                        if (cn.indexOf("WeakReference") >= 0) act = wr.get();
                    } catch (e) {}
                    if (!act) continue;
                    if (tryOne("mActivities", act)) return true;
                } catch (e) {}
            }
        } catch (e) {}
        return false;
    }

    function tryAttachOnce() {
        attachAttempts++;
        Java.performNow(function () {
            if (attachDone) return;
            if (tryUnityPlayer()) return;
            if (tryMActivities()) return;
        });
    }

    // ═══════════════════════════════════════════════════════════════
    //  Activity.onResume hook
    // ═══════════════════════════════════════════════════════════════
    var hooksInstalled = false;
    function installAllHooks() {
        if (hooksInstalled) return;
        hooksInstalled = true;
        try {
            Java.performNow(function () {
                try {
                    var Activity = Java.use("android.app.Activity");
                    var origOnResume = Activity.onResume;
                    Activity.onResume.implementation = function () {
                        try { origOnResume.call(this); } catch (e) {}
                        if (!attachDone) {
                            try { tryOne("onResume", this); } catch (e) {}
                        }
                    };
                    send({type:"hook_installed"});
                } catch (e) {
                    send({type:"hook_error", message:"install: " + e});
                }
            });
        } catch (e) { send({type:"hook_outer_error", message:""+e}); }
    }

    // ═══════════════════════════════════════════════════════════════
    //  POLLER — THE event loop. No setTimeout anywhere.
    // ═══════════════════════════════════════════════════════════════
    var pollCount = 0;
    var POLL_INTERVAL_MS = 50;
    var ALIVE_EVERY_MS = 30000;
    var lastAliveAt = 0;
    var booted = false;

    function pollTick() {
        try {
            Java.performNow(function () {
                var MV = getModViewClass();
                if (!MV) return;

                // One-shot boot: install hooks + first attach attempt
                if (!booted) {
                    booted = true;
                    try { send({type:"boot_poller_v5"}); } catch (e) {}
                    installAllHooks();
                    tryAttachOnce();
                    try { send({type:"boot_poller_done_v5"}); } catch (e) {}
                }

                // Keep trying attach while not attached
                if (!attachDone) {
                    try { tryAttachOnce(); } catch (e) {}
                }

                // Drain the C++ queue aggressively
                var drained = 0;
                for (var i = 0; i < 128; i++) {
                    var raw;
                    try { raw = MV.nativeGetPendingCmd(); }
                    catch (e) { break; }
                    if (!raw) break;
                    var obj = null;
                    try { obj = JSON.parse(raw); } catch (e) { continue; }
                    var cmd = (obj.payload && obj.payload.action)
                        ? obj.payload : obj;
                    if (!cmd || !cmd.action) continue;
                    dispatchCmd(cmd);
                    drained++;
                }

                pollCount++;
                var now = Date.now();
                if ((now - lastAliveAt) >= ALIVE_EVERY_MS) {
                    lastAliveAt = now;
                    try { send({type:"poller_alive", count:pollCount,
                                drained:drained}); } catch (e) {}
                }
            });
        } catch (e) {
            try { send({type:"tick_err", message:""+e}); } catch (e2) {}
        }
    }

    try { send({type:"poller_started", interval:POLL_INTERVAL_MS}); } catch (e) {}
    setInterval(pollTick, POLL_INTERVAL_MS);

    // Immediate signals (no timers)
    try { send({type:"cpp_ready"}); }       catch (e) {}
    try { send({type:"cpp_ready_final"}); } catch (e) {}
    try { send({type:"boot_signals_done_v5"}); } catch (e) {}
})();
