// ===========================================================================
// YAM-GG bootstrap.js
// Single responsibility: register cpp_* command handlers on the embedded
// Java bridge; expose a poller that drains the C++ command queue; auto
// attach ModView to the foreground Activity.
// ===========================================================================
(function () {
    "use strict";

    // ─── Handle registry (never leak, FinalizationRegistry when available) ─
    var table = Object.create(null);
    var nextHandle = 1;
    var registry = (typeof FinalizationRegistry === "function")
        ? new FinalizationRegistry(function (id) {
            if (Object.prototype.hasOwnProperty.call(table, id)) delete table[id];
        })
        : null;

    function alloc(o) {
        var id = nextHandle++;
        table[id] = o;
        if (registry && o !== null && typeof o === "object") {
            try { registry.register(o, id); } catch (e) {}
        }
        return id;
    }
    function get(id) {
        return Object.prototype.hasOwnProperty.call(table, id) ? table[id] : null;
    }
    function drop(id) {
        if (Object.prototype.hasOwnProperty.call(table, id)) {
            var o = table[id];
            if (registry && o !== null && typeof o === "object") {
                try { registry.unregister(o); } catch (e) {}
            }
            delete table[id];
        }
    }
    function dropAll() {
        table = Object.create(null);
        nextHandle = 1;
    }

    var LB = "\u005B";
    var RB = "\u005D";

    // ─── Reply envelope ─────────────────────────────────────────────────
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

    // ─── JNI signature parser ───────────────────────────────────────────
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

    // ─── JSON → Java materializer ───────────────────────────────────────
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
        case "handle_array": {
            var hElType = v.elementType || "java.lang.Object";
            return Java.array(hElType,
                              (v.handles || []).map(function (h) { return get(h); }));
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
            if (v.fields) {
                for (var fi = 0; fi < v.fields.length; fi++) {
                    inst[v.fields[fi].name] = mat(v.fields[fi].value);
                }
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

    // ─── describe (cycle-safe) ─────────────────────────────────────────
    function describe(v) {
        return describeRec(v, new WeakSet(), 0);
    }
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

    // ─── Chunked sender ────────────────────────────────────────────────
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
    //  cpp_* HANDLERS
    // ═══════════════════════════════════════════════════════════════
    var handlers = {
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
        cpp_cast: function (cmd) {
            var cls = get(cmd.classHandle);
            var obj = get(cmd.objectHandle);
            if (!cls || !obj) throw new Error("cast: missing handle");
            replyHandle(cmd.id, alloc(Java.cast(obj, cls)));
        },
        cpp_create_string: function (cmd) {
            replyHandle(cmd.id,
                alloc(Java.use("java.lang.String").$new(cmd.value)));
        },
        cpp_new_instance: function (cmd) {
            var cls = get(cmd.classHandle);
            if (!cls) throw new Error("no class handle");
            var args = (cmd.args || []).map(mat);
            var inst;
            if (cmd.ctorSig && cmd.ctorSig.length > 0) {
                inst = cls.$alloc();
                cls.$init.overload.apply(cls.$init, parseSig(cmd.ctorSig))
                    .call(inst, ...args);
            } else {
                inst = cls.$new.apply(cls, args);
            }
            if (cmd.fields) {
                for (var i = 0; i < cmd.fields.length; i++) {
                    inst[cmd.fields[i].name] = mat(cmd.fields[i].value);
                }
            }
            replyHandle(cmd.id, alloc(inst));
        },
        cpp_array_of: function (cmd) {
            var elType = cmd.elementType || "java.lang.Object";
            var elems = (cmd.elements || []).map(mat);
            replyHandle(cmd.id, alloc(Java.array(elType, elems)));
        },
        cpp_array_length: function (cmd) {
            var arr = get(cmd.handle);
            replyValue(cmd.id, arr ? Number(arr.length) : 0);
        },
        cpp_array_get: function (cmd) {
            var arr = get(cmd.handle);
            var el = arr ? arr[cmd.index] : null;
            if (el === null || el === undefined) { replyValue(cmd.id, null); return; }
            if (typeof el === "object" && el.$className)
                replyHandle(cmd.id, alloc(el));
            else replyValue(cmd.id, el);
        },
        cpp_array_set: function (cmd) {
            var arr = get(cmd.handle);
            if (!arr) throw new Error("no array");
            arr[cmd.index] = mat(cmd.value);
            replyVoid(cmd.id);
        },
        cpp_release_handle: function (cmd) {
            drop(cmd.handle);
            replyVoid(cmd.id);
        },
        cpp_list_handles: function (cmd) {
            var list = [];
            for (var k in table) {
                if (!Object.prototype.hasOwnProperty.call(table, k)) continue;
                var o = table[k];
                list.push({id:Number(k), kind:"java",
                           className:(o && o.$className) || ""});
            }
            replyValue(cmd.id, list);
        },
        cpp_clear_handles: function (cmd) { dropAll(); replyVoid(cmd.id); },

        cpp_get_field: function (cmd) {
            var cls = Java.use(cmd.className);
            var f = cls[cmd.fieldName];
            if (!f) throw new Error("no field: " + cmd.fieldName);
            var isStatic = false;
            try {
                var fields = cls.class.getDeclaredFields();
                for (var i = 0; i < fields.length; i++) {
                    if (String(fields[i].getName()) === cmd.fieldName) {
                        isStatic = (Number(fields[i].getModifiers()) & 8) !== 0;
                        break;
                    }
                }
            } catch (e) {}
            replyValue(cmd.id,
                JSON.stringify({static:isStatic, handle:alloc(f)}));
        },

        cpp_probe_class: function (cmd) {
            var c = Java.use(cmd.className);
            var methods = [];
            var ms = c.class.getDeclaredMethods();
            for (var i = 0; i < ms.length; i++) {
                try { ms[i].setAccessible(true); } catch (e2) {}
                var pts = ms[i].getParameterTypes();
                var args = [];
                for (var j = 0; j < pts.length; j++) {
                    args.push({name:"arg"+j,
                               typeName:String(pts[j].getName())});
                }
                methods.push({
                    name: String(ms[i].getName()),
                    ret:  String(ms[i].getReturnType().getName()),
                    isStatic: (Number(ms[i].getModifiers()) & 8) !== 0,
                    args: args
                });
            }
            var fields = [];
            var fs = c.class.getDeclaredFields();
            for (var k = 0; k < fs.length; k++) {
                fields.push({
                    name: String(fs[k].getName()),
                    type: String(fs[k].getType().getName()),
                    value: ""
                });
            }
            var ctors = [];
            try {
                var cs = c.class.getDeclaredConstructors();
                for (var q = 0; q < cs.length; q++) {
                    var ptypes = cs[q].getParameterTypes();
                    var cargs = [];
                    for (var w = 0; w < ptypes.length; w++) {
                        cargs.push({name:"arg"+w,
                                    typeName:String(ptypes[w].getName())});
                    }
                    ctors.push({params:cargs});
                }
            } catch (e3) {}
            sendMaybeChunked({type:"class_probe", className:cmd.className,
                              methods:methods, fields:fields,
                              constructors:ctors});
            replyValue(cmd.id, JSON.stringify({
                methods: methods.length, fields: fields.length
            }));
        },

        cpp_call_method: function (cmd) {
            var m = get(cmd.methodHandle);
            if (!m) throw new Error("no method handle");
            var inst = cmd.instanceHandle ? get(cmd.instanceHandle) : null;
            var args = (cmd.args || []).map(mat);
            var r = inst ? m.apply(inst, args) : m.apply(null, args);
            if (r === null || r === undefined) replyValue(cmd.id, null);
            else if (typeof r === "object" && r.$className)
                replyHandle(cmd.id, alloc(r));
            else replyValue(cmd.id, r);
        },

        cpp_eval: function (cmd) {
            var t0 = Date.now();
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                var r = fn();
                var rs = describe(r);
                send({type:"eval_result", id:cmd.id, ok:true,
                      result:rs, durationMs:Date.now()-t0});
                reply(cmd.id, true, "value", rs);
            } catch (e) {
                var es = "" + e;
                send({type:"eval_result", id:cmd.id, ok:false,
                      error:es, durationMs:Date.now()-t0});
                replyError(cmd.id, e);
            }
        },

        cpp_noop: function (cmd) { replyVoid(cmd.id); },
        cpp_pong: function (cmd) { replyVoid(cmd.id); }
    };

    // ═══════════════════════════════════════════════════════════════
    //  NATIVE handlers (top-level actions)
    // ═══════════════════════════════════════════════════════════════
    var nativeHandlers = {
        eval: function (cmd) {
            var id = cmd.id, t0 = Date.now();
            try {
                var fn = new Function("return (" + (cmd.code || "null") + ");");
                var r = fn();
                send({type:"eval_result", id:id, ok:true,
                      result:describe(r), durationMs:Date.now()-t0});
            } catch (e) {
                send({type:"eval_result", id:id, ok:false,
                      error:""+e, durationMs:Date.now()-t0});
            }
        },
        run_user_script: function (cmd) {
            var t0 = Date.now(), nm = cmd.name || "anon";
            var code = cmd.code || "";
            try {
                Java.performNow(function () { (new Function(code))(); });
                send({type:"user_script_loaded", name:nm,
                      size:code.length, ok:true, durationMs:Date.now()-t0});
            } catch (e) {
                send({type:"user_script_loaded", name:nm,
                      ok:false, error:""+e, durationMs:Date.now()-t0});
                send({type:"script_failed", name:nm, error:""+e});
            }
        },
        enumerate_classes: function (cmd) {
            try {
                var all = Java.enumerateLoadedClassesSync();
                sendMaybeChunked({type:"classes_list",
                                  count:all.length, classes:all});
            } catch (e) {
                send({type:"classes_list", count:0, classes:[],
                      error:""+e});
            }
        },
        enumerate_loaders: function (cmd) {
            try {
                var ls = Java.enumerateClassLoadersSync();
                var out = [];
                for (var i = 0; i < ls.length; i++) {
                    try { out.push(String(ls[i])); } catch (e) {}
                }
                sendMaybeChunked({type:"loaders_list",
                                  count:out.length, loaders:out});
            } catch (e) {
                send({type:"loaders_list", count:0, loaders:[]});
            }
        },
        backtrace: function (cmd) {
            try {
                var limit = cmd.limit || 8;
                var bt = Java.backtrace({limit:limit});
                var frames = [];
                if (bt && bt.frames) {
                    for (var i = 0; i < bt.frames.length; i++) {
                        var f = bt.frames[i];
                        frames.push(String(f.signature ||
                            (f.className + "." + f.methodName)));
                    }
                }
                sendMaybeChunked({type:"backtrace_result", frames:frames});
            } catch (e) {
                send({type:"backtrace_result", frames:[]});
            }
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  DISPATCHER
    // ═══════════════════════════════════════════════════════════════
    function unwrapCmd(msg) {
        if (!msg) return null;
        if (typeof msg === "string") {
            try { return JSON.parse(msg); } catch (e) { return null; }
        }
        if (msg.payload !== undefined && msg.payload !== null)
            return msg.payload;
        if (msg.action) return msg;
        return null;
    }
    function dispatchCmd(cmd) {
        if (!cmd || !cmd.action) return;
        var h = handlers[cmd.action];
        if (h) {
            try { h(cmd); }
            catch (e) { try { replyError(cmd.id, e); } catch (e2) {} }
            return;
        }
        var nh = nativeHandlers[cmd.action];
        if (nh) {
            try { nh(cmd); }
            catch (e) { try { replyError(cmd.id, e); } catch (e2) {} }
            return;
        }
        if (cmd.action === "yamgg_tick" || cmd.action === "yamgg_boot")
            return;
        try {
            replyError(cmd.id, new Error("unknown: " + cmd.action));
        } catch (e) {}
    }

    // recv channel (frida-native)
    try {
        recv("yamgg_cmd", function (msg) {
            dispatchCmd(unwrapCmd(msg));
        });
    } catch (e) {}

    // ═══════════════════════════════════════════════════════════════
    //  MODVIEW CACHE (avoid Java.use per poll tick)
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
    //  ACTIVITY DISCOVERY + ATTACH
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
            if (tryUnityPlayer())   return;
            if (tryMActivities())   return;
        });
    }

    function loopAttach() {
        if (attachDone) return;
        try { tryAttachOnce(); }
        catch (e) { send({type:"attach_outer_err", message:""+e}); }
        setTimeout(loopAttach, 1000);
    }

    // ═══════════════════════════════════════════════════════════════
    //  POLLER — drains the C++ cmd queue
    // ═══════════════════════════════════════════════════════════════
    var pollInstalled = false;
    var pollCount = 0;
    var POLL_INTERVAL_MS = 250;
    var ALIVE_EVERY_MS = 30000;
    var lastAliveAt = 0;

    function pollerOnce() {
        Java.performNow(function () {
            var MV = getModViewClass();
            if (!MV) return;

            var drained = 0;
            for (var i = 0; i < 64; i++) {
                var raw;
                try { raw = MV.nativeGetPendingCmd(); }
                catch (e) { send({type:"poller_err", message:""+e}); break; }
                if (!raw) break;

                var obj = null;
                try { obj = JSON.parse(raw); }
                catch (e) { continue; }

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
                send({type:"poller_alive", count:pollCount, drained:drained});
            }
        });
    }

    function installPoller() {
        if (pollInstalled) return;
        pollInstalled = true;
        send({type:"poller_started", mode:"setInterval",
              interval:POLL_INTERVAL_MS});
        setInterval(function () {
            try { pollerOnce(); }
            catch (e) { send({type:"tick_err", message:""+e}); }
        }, POLL_INTERVAL_MS);
    }

    // ═══════════════════════════════════════════════════════════════
    //  Activity.onResume hook
    // ═══════════════════════════════════════════════════════════════
    function installAllHooks() {
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
    //  READY + BOOT
    // ═══════════════════════════════════════════════════════════════
    try { send({ type: "cpp_ready" }); }       catch (e) {}
    try { send({ type: "cpp_ready_final" }); } catch (e) {}

    setTimeout(function () {
        try { send({type:"boot_running"}); } catch (e) {}

        try { installAllHooks(); } catch (e) {}
        try { installPoller(); }   catch (e) {}
        try { tryAttachOnce(); }   catch (e) {}
        try { loopAttach(); }      catch (e) {}

        try { send({ type: "boot_done" }); } catch (e) {}
    }, 800);
})();
