#ifndef YAM_WRAPPER_H
#define YAM_WRAPPER_H

// ===========================================================================
// yam_wrapper.h — aggregate header for the YAM-GG wrapper.
//
// Composes:
//   1. YAMJS.h       — the library's unified public header. Provides every
//                      core symbol: script, interceptor, memory, module,
//                      symbol, stalker, backtracer, api resolver, thread
//                      registry, elf module, tls key, event sink, kernel,
//                      linux helpers, android helpers, arm64 writer,
//                      code segment, metal, sanity/sampler/profiler.
//
//   2. Extra subsystem declarations the wrapper needs that YAMJS.h does not
//      expose: CModule, BoundsChecker, AllocatorProbe, SourceMap,
//      AllocationTracker, InstanceTracker, Arm64Relocator. Signatures are
//      the wrapper's own (verified against nm on libyamjs.a).
//
//   3. A handful of symbol aliases for names the library exports under a
//      different identifier (GObject-managed subsystems).
// ===========================================================================

#include "YAMJS.h"

extern "C" {

// ---------------------------------------------------------------------------
// Symbols used by the wrapper that YAMJS.h does not declare.
// Weak: link succeeds if the library provides them, and the build fails
// with an explicit "undefined reference" otherwise.
// ---------------------------------------------------------------------------
extern gpointer yam_object_unref (gpointer obj) __attribute__((weak));

// ---------------------------------------------------------------------------
// CModule — runtime C compilation
// ---------------------------------------------------------------------------
typedef struct _YamCModule YamCModule;
typedef struct _YamCModuleOptions {
    YamMemoryRange range;
    gpointer       allocator;
    gpointer       resolver;
} YamCModuleOptions;

YamCModule*     yam_cmodule_new       (const gchar* source,
                                        const YamCModuleOptions* options,
                                        const gchar* name);
gboolean        yam_cmodule_link      (YamCModule* self);
YamMemoryRange* yam_cmodule_get_range (YamCModule* self);
gpointer        yam_cmodule_find_symbol_by_name (YamCModule* self, const gchar* name);
void            yam_cmodule_add_symbol (YamCModule* self, const gchar* name, gpointer value);
void            yam_cmodule_drop_metadata (YamCModule* self);

typedef struct _YamCModuleSymbolDetails {
    const gchar* name;
    gpointer     value;
} YamCModuleSymbolDetails;

typedef gboolean (*YamCModuleSymbolVisitor)(const YamCModuleSymbolDetails* d, gpointer user);
void yam_cmodule_enumerate_symbols (YamCModule* self, YamCModuleSymbolVisitor v, gpointer user);

// ---------------------------------------------------------------------------
// BoundsChecker
// ---------------------------------------------------------------------------
typedef struct _YamBoundsChecker YamBoundsChecker;

YamBoundsChecker* yam_bounds_checker_new (gpointer filter_function, gpointer user_data);
void yam_bounds_checker_attach  (YamBoundsChecker* self, const gchar* apis);
void yam_bounds_checker_attach_to_apis (YamBoundsChecker* self, const gchar* apis);
void yam_bounds_checker_detach  (YamBoundsChecker* self);
void yam_bounds_checker_set_front_alignment (YamBoundsChecker* self, guint granularity);
void yam_bounds_checker_set_pool_size       (YamBoundsChecker* self, guint pool_size);
guint yam_bounds_checker_get_front_alignment(YamBoundsChecker* self);
guint yam_bounds_checker_get_pool_size      (YamBoundsChecker* self);

// ---------------------------------------------------------------------------
// AllocatorProbe
// ---------------------------------------------------------------------------
typedef struct _YamAllocatorProbe YamAllocatorProbe;

YamAllocatorProbe* yam_allocator_probe_new (void);
void yam_allocator_probe_attach         (YamAllocatorProbe* self);
void yam_allocator_probe_attach_to_apis (YamAllocatorProbe* self, const gchar* apis);
void yam_allocator_probe_detach         (YamAllocatorProbe* self);
void yam_allocator_probe_suppress       (YamAllocatorProbe* self);

// ---------------------------------------------------------------------------
// SourceMap
// ---------------------------------------------------------------------------
typedef struct _YamSourceMap YamSourceMap;

YamSourceMap* yam_source_map_new (const gchar* json);
gboolean yam_source_map_resolve (YamSourceMap* self,
                                  guint line, guint column,
                                  guint* out_line, guint* out_column,
                                  const gchar** out_name,
                                  const gchar** out_source);

// ---------------------------------------------------------------------------
// AllocationTracker
// ---------------------------------------------------------------------------
typedef struct _YamAllocationTracker YamAllocationTracker;
typedef struct _YamAllocationBlock   YamAllocationBlock;
typedef struct _YamAllocationGroup   YamAllocationGroup;

YamAllocationTracker* yam_allocation_tracker_new (void);
void yam_allocation_tracker_begin (YamAllocationTracker* self, guint flags);
gboolean yam_allocation_tracker_end (YamAllocationTracker* self);
void yam_allocation_tracker_on_malloc  (YamAllocationTracker* self, gpointer address, gsize size);
void yam_allocation_tracker_on_free    (YamAllocationTracker* self, gpointer address);
void yam_allocation_tracker_on_realloc (YamAllocationTracker* self, gpointer old_address, gpointer new_address, gsize new_size);
guint yam_allocation_tracker_peek_block_count      (YamAllocationTracker* self);
gsize yam_allocation_tracker_peek_block_total_size (YamAllocationTracker* self);
void yam_allocation_tracker_peek_block_list  (YamAllocationTracker* self, YamAllocationBlock** out, guint* count);
void yam_allocation_tracker_peek_block_groups(YamAllocationTracker* self, YamAllocationGroup** out, guint* count);
void yam_allocation_block_list_free  (YamAllocationBlock* list);
void yam_allocation_group_list_free  (YamAllocationGroup* list);

// ---------------------------------------------------------------------------
// InstanceTracker
// ---------------------------------------------------------------------------
typedef struct _YamInstanceTracker YamInstanceTracker;

YamInstanceTracker* yam_instance_tracker_new (void);
void yam_instance_tracker_begin (YamInstanceTracker* self, guint flags);
gboolean yam_instance_tracker_end (YamInstanceTracker* self);
void yam_instance_tracker_set_type_filter_function (YamInstanceTracker* self, gpointer filter, gpointer user);
void yam_instance_tracker_add_instance    (YamInstanceTracker* self, gpointer instance, gpointer type);
void yam_instance_tracker_remove_instance (YamInstanceTracker* self, gpointer instance, gpointer type);
guint yam_instance_tracker_peek_total_count (YamInstanceTracker* self);
gpointer yam_instance_tracker_get_current_vtable (YamInstanceTracker* self);

typedef gboolean (*YamInstanceVisitor)(gpointer instance, gpointer type, gpointer user);
void yam_instance_tracker_walk_instances (YamInstanceTracker* self, YamInstanceVisitor v, gpointer user);

// ---------------------------------------------------------------------------
// Arm64Relocator
// ---------------------------------------------------------------------------
typedef struct _YamArm64Relocator YamArm64Relocator;

YamArm64Relocator* yam_arm64_relocator_new (gpointer input_code, YamArm64Writer* output);
void yam_arm64_relocator_init (YamArm64Relocator* self, gpointer input_code, YamArm64Writer* output);
void yam_arm64_relocator_clear(YamArm64Relocator* self);
void yam_arm64_relocator_reset(YamArm64Relocator* self, gpointer input_code, YamArm64Writer* output);
guint yam_arm64_relocator_read_one (YamArm64Relocator* self);
void yam_arm64_relocator_write_all (YamArm64Relocator* self);
void yam_arm64_relocator_write_one (YamArm64Relocator* self);
void yam_arm64_relocator_skip_one (YamArm64Relocator* self);
gboolean yam_arm64_relocator_eob (YamArm64Relocator* self);
gboolean yam_arm64_relocator_eoi (YamArm64Relocator* self);
void yam_arm64_relocator_set_scratch_reg (YamArm64Relocator* self, guint reg);
guint yam_arm64_relocator_pick_exit_reg   (YamArm64Relocator* self);
void yam_arm64_relocator_set_code_range  (YamArm64Relocator* self, gpointer start, gpointer end);
gboolean yam_arm64_relocator_can_relocate (gpointer address, guint min_bytes, guint max_range, gpointer* out_code);
gboolean yam_arm64_relocator_can_relocate_within (gpointer address, guint min_bytes, YamMemoryRange* range);

// ---------------------------------------------------------------------------
// Aliases for GObject-managed subsystems: the library uses yam_object_unref.
// ---------------------------------------------------------------------------
#define yam_cmodule_free(o)           yam_object_unref(o)
#define yam_bounds_checker_destroy(o) yam_object_unref(o)
#define yam_source_map_free(o)        yam_object_unref(o)
#define yam_arm64_relocator_free(o)   yam_object_unref(o)

} // extern "C"

#endif // YAM_WRAPPER_H
