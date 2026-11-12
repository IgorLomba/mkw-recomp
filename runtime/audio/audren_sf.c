// The SDK's audio service objects, made by the host. nn::audio::CreateAudioRendererManager(bool), replaced by name,
// hands the nn::audio client an IAudioRendererManager whose vtable holds host functions, so OpenAudioRenderer,
// RequestUpdate and the device calls arrive here as C calls on audren.c and audio_mixer.c, with no IPC message,
// session or service name in between (Adrià, 05/11/2026: the SDK replaced by name, no console service imitated).
//
// The sf object ABI of MKW's SDK 19.3.3, read from the lifted client: a guest object
// whose first word is its vtable; slot 0 AddReference(), 1 Release(), 2-3 never called by the client, then one slot
// per method in command order (IAudioDevice has no command 9). x0 is the object; an Out<T> of plain data is a T*, an
// In struct a pointer to it, an InBuffer / OutBuffer a pointer to {addr, size}, a NativeHandle (in or out) a pointer
// to {u32 handle, u8 managed}, an Out<SharedPointer<I>> a pointer to the SharedPointer's address; a float argument
// comes in s0 after the integer ones. Methods return nn::Result in w0. The signatures are those of the SDK's own
// server classes, whose symbols the sdk keeps (nn::audio::server::AudioRendererManagerImpl::*). A slot no client call
// of MKW reaches is a named fatal, not a guess.
#include <stdlib.h>
#include <string.h>

#include "audren.h"
#include "host.h"
#include "platform/platform.h"

typedef struct { uint64_t vtable; void *host; int32_t refs, pad; } SfObject;   // in guest memory

enum { MANAGER_SLOTS = 9, RENDERER_SLOTS = 18, DEVICE_SLOTS = 24 };
static uint64_t *manager_vt, *renderer_vt, *device_vt;
static SfObject *manager, *device;
static PlatOnce once;

void *audren_guest(uint64_t addr, uint64_t size) {
    if ((!addr && size) || addr + size < addr || addr + size > RT_ADDRESS_LIMIT)
        rt_fatal("audio: client buffer %#llx+%#llx", (unsigned long long)addr, (unsigned long long)size);
    return (void *)(uintptr_t)addr;
}

static void ok(Cpu *c) { ret_x(c, 0); }
static AudioRenderer *renderer_of(const Cpu *c) { return ((SfObject *)arg_p(c, 0))->host; }

static SfObject *object_new(const uint64_t *vtable, void *host) {
    SfObject *o = mem_kernel_alloc(sizeof *o, 16);
    *o = (SfObject){(uint64_t)(uintptr_t)vtable, host, 1, 0};
    return o;
}

static void out_object(const Cpu *c, int i, const SfObject *o) {   // Out<SharedPointer<I>>
    ST64(LD64(arg_x(c, i)), (uint64_t)(uintptr_t)o);
}

static void out_handle(const Cpu *c, int i, uint32_t h) {   // Out<NativeHandle>: the client closes it (managed)
    uint64_t p = arg_x(c, i);
    ST32(p, h);
    ST8(p + 4, 1);
}

#define UNSERVED(iface)                                                                                             \
    static void unserved_##iface(Cpu *c) {                                                                         \
        char lr[160];                                                                                              \
        rt_fatal("audio: " #iface " method not served by the host, called from %s",                              \
                 ldr_describe(c->x[30] - 4, lr, sizeof lr));                                                       \
    }                                                                                                              \
    HOST_FUNCTION("audio " #iface " unserved", unserved_##iface)
UNSERVED(IAudioRendererManager);
UNSERVED(IAudioRenderer);
UNSERVED(IAudioDevice);

static void shared_ref(Cpu *c) { (void)c; }   // the manager and the device live as long as the process
HOST_FUNCTION("audio shared AddReference/Release", shared_ref);

// ---- IAudioRendererManager
// OpenAudioRenderer(Out<SharedPointer<IAudioRenderer>>, AudioRendererParameterInternal const&, NativeHandle
// transfer memory, NativeHandle process, u64 size, AppletResourceUserId, ...)
static void manager_open(Cpu *c) {
    uint32_t h = LD32(arg_x(c, 3));
    uint64_t size = arg_x(c, 5);
    const KernTransferMemory *tm = kern_handle_get(h, KERN_TMEM);
    if (!tm || tm->size < size) rt_fatal("audio: OpenAudioRenderer work buffer is not a transfer memory of %#llx bytes",
                                         (unsigned long long)size);
    out_object(c, 1, object_new(renderer_vt, audren_open(arg_p(c, 2), tm->addr, size)));
    ok(c);
}
HOST_FUNCTION("audio IAudioRendererManager OpenAudioRenderer", manager_open);

static void manager_work_size(Cpu *c) {   // GetWorkBufferSize(Out<long>, AudioRendererParameterInternal const&)
    ST64(arg_x(c, 1), audren_work_buffer_size(arg_p(c, 2)));
    ok(c);
}
HOST_FUNCTION("audio IAudioRendererManager GetWorkBufferSize", manager_work_size);

static void manager_device(Cpu *c) {   // GetAudioDeviceService(WithRevisionInfo)(Out<SharedPointer<IAudioDevice>>, ...)
    out_object(c, 1, device);
    ok(c);
}
HOST_FUNCTION("audio IAudioRendererManager GetAudioDeviceService", manager_device);

// ---- IAudioRenderer
static void renderer_add_ref(Cpu *c) { __atomic_fetch_add(&((SfObject *)arg_p(c, 0))->refs, 1, __ATOMIC_RELAXED); }
HOST_FUNCTION("audio IAudioRenderer AddReference", renderer_add_ref);

static void renderer_release(Cpu *c) {
    SfObject *o = arg_p(c, 0);
    if (__atomic_sub_fetch(&o->refs, 1, __ATOMIC_ACQ_REL)) return;
    audren_close(o->host);
    mem_kernel_free(o, sizeof *o);
}
HOST_FUNCTION("audio IAudioRenderer Release", renderer_release);

static void renderer_sample_rate(Cpu *c) { ST32(arg_x(c, 1), (uint32_t)renderer_of(c)->param.sample_rate), ok(c); }
static void renderer_sample_count(Cpu *c) { ST32(arg_x(c, 1), (uint32_t)renderer_of(c)->param.sample_count), ok(c); }
static void renderer_mix_buffers(Cpu *c) { ST32(arg_x(c, 1), (uint32_t)renderer_of(c)->param.mix_buffer_count), ok(c); }
static void renderer_state(Cpu *c) { ST32(arg_x(c, 1), renderer_of(c)->state), ok(c); }
HOST_FUNCTION("audio IAudioRenderer GetSampleRate", renderer_sample_rate);
HOST_FUNCTION("audio IAudioRenderer GetSampleCount", renderer_sample_count);
HOST_FUNCTION("audio IAudioRenderer GetMixBufferCount", renderer_mix_buffers);
HOST_FUNCTION("audio IAudioRenderer GetState", renderer_state);

static void renderer_update(Cpu *c) {   // RequestUpdate(Auto)(OutBuffer output, OutBuffer performance, InBuffer input)
    audren_update(renderer_of(c), arg_p(c, 1), arg_p(c, 2), arg_p(c, 3));
    ok(c);
}
HOST_FUNCTION("audio IAudioRenderer RequestUpdate", renderer_update);

static void renderer_start(Cpu *c) { audren_set_running(renderer_of(c), 1), ok(c); }
static void renderer_stop(Cpu *c) { audren_set_running(renderer_of(c), 0), ok(c); }
static void renderer_event(Cpu *c) { out_handle(c, 1, kern_event_readable(renderer_of(c)->event)), ok(c); }
HOST_FUNCTION("audio IAudioRenderer Start", renderer_start);
HOST_FUNCTION("audio IAudioRenderer Stop", renderer_stop);
HOST_FUNCTION("audio IAudioRenderer QuerySystemEvent", renderer_event);

static void renderer_set_limit(Cpu *c) { renderer_of(c)->time_limit = (uint32_t)arg_x(c, 1), ok(c); }
static void renderer_get_limit(Cpu *c) {
    uint32_t v = renderer_of(c)->time_limit;
    ST32(arg_x(c, 1), v ? v : 100);
    ok(c);
}
HOST_FUNCTION("audio IAudioRenderer SetRenderingTimeLimit", renderer_set_limit);
HOST_FUNCTION("audio IAudioRenderer GetRenderingTimeLimit", renderer_get_limit);

// SetVoiceDropParameter / GetVoiceDropParameter (float): the host never drops voices; the value is kept.
static void renderer_set_drop(Cpu *c) { renderer_of(c)->voice_drop = arg_s(c, 0), ok(c); }
static void renderer_get_drop(Cpu *c) {
    float v = renderer_of(c)->voice_drop;
    uint32_t b;
    memcpy(&b, &v, 4);
    ST32(arg_x(c, 1), b);
    ok(c);
}
HOST_FUNCTION("audio IAudioRenderer SetVoiceDropParameter", renderer_set_drop);
HOST_FUNCTION("audio IAudioRenderer GetVoiceDropParameter", renderer_get_drop);

// ---- IAudioDevice: one stereo output device that never changes
static uint32_t change_event;   // the device change events: never signalled

static void put_name(uint64_t buffer) {   // OutBuffer of AudioDeviceName (0x100 bytes)
    const AudrenBuffer *b = (const AudrenBuffer *)(uintptr_t)buffer;
    if (!b->size) return;
    char *dst = audren_guest(b->addr, b->size);
    memset(dst, 0, b->size);
    strncpy(dst, audio_device_name, b->size - 1);
}

static void device_list(Cpu *c) {   // ListAudioDeviceName(Auto), ListAudioOutputDeviceName(OutBuffer, Out<int>)
    const AudrenBuffer *b = arg_p(c, 1);
    int32_t n = b->size >= 0x100;
    if (n) put_name(arg_x(c, 1));
    ST32(arg_x(c, 2), (uint32_t)n);
    ok(c);
}
static void device_set_volume(Cpu *c) { audio_device_set_volume(arg_s(c, 0)), ok(c); }   // (InBuffer name, float)
static void device_get_volume(Cpu *c) {   // (InBuffer name, Out<float>)
    float v = audio_device_volume();
    uint32_t b;
    memcpy(&b, &v, 4);
    ST32(arg_x(c, 2), b);
    ok(c);
}
static void device_active_name(Cpu *c) { put_name(arg_x(c, 1)), ok(c); }   // GetActiveAudio(Output)DeviceName(Auto)
static void device_event(Cpu *c) {   // Query*Event(Out<NativeHandle>)
    uint32_t ev = __atomic_load_n(&change_event, __ATOMIC_ACQUIRE);
    if (!ev) {
        uint32_t mine = kern_event_new(), expected = 0;
        ev = __atomic_compare_exchange_n(&change_event, &expected, mine, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
                 ? mine
                 : expected;   // the loser's event stays unused
    }
    out_handle(c, 1, kern_event_readable(ev));
    ok(c);
}
static void device_channels(Cpu *c) { ST32(arg_x(c, 1), audio_output_channels()), ok(c); }   // GetActiveChannelCount
// Switch 2: SetAudioDeviceOutputVolumeAutoTuneEnabled(bool) / IsAudioDeviceOutputVolumeAutoTuneEnabled(Out<bool>):
// no automatic volume tuning here; setting it changes nothing.
static void device_set_autotune(Cpu *c) { ok(c); }
static void device_get_autotune(Cpu *c) { ST8(arg_x(c, 1), 0), ok(c); }
HOST_FUNCTION("audio IAudioDevice ListAudioDeviceName", device_list);
HOST_FUNCTION("audio IAudioDevice SetAudioDeviceOutputVolume", device_set_volume);
HOST_FUNCTION("audio IAudioDevice GetAudioDeviceOutputVolume", device_get_volume);
HOST_FUNCTION("audio IAudioDevice GetActiveAudioDeviceName", device_active_name);
HOST_FUNCTION("audio IAudioDevice QueryAudioDeviceEvent", device_event);
HOST_FUNCTION("audio IAudioDevice GetActiveChannelCount", device_channels);
HOST_FUNCTION("audio IAudioDevice SetAudioDeviceOutputVolumeAutoTuneEnabled", device_set_autotune);
HOST_FUNCTION("audio IAudioDevice IsAudioDeviceOutputVolumeAutoTuneEnabled", device_get_autotune);

// ---- vtables
static uint64_t *vtable(int n, GuestFn add_ref, GuestFn release, GuestFn unserved, const GuestFn *methods, int nm) {
    uint64_t *vt = mem_kernel_alloc(8u * (uint64_t)n, 16);
    vt[0] = hostfn_address(add_ref), vt[1] = hostfn_address(release);
    for (int i = 2; i < n; i++) vt[i] = hostfn_address(i - 4 < nm && i >= 4 && methods[i - 4] ? methods[i - 4] : unserved);
    return vt;
}

static void build(void *arg) {
    (void)arg;
    // Slot 4 + command; NULL: not served (OpenAudioRendererForManualExecution, ExecuteAudioRendererRendering, the
    // device notifications 15-18, which no MKW call reaches).
    static const GuestFn mgr[] = {manager_open, manager_work_size, manager_device, NULL, manager_device};
    static const GuestFn ren[] = {renderer_sample_rate, renderer_sample_count, renderer_mix_buffers, renderer_state,
                                  renderer_update, renderer_start, renderer_stop, renderer_event, renderer_set_limit,
                                  renderer_get_limit, renderer_update, NULL, renderer_set_drop, renderer_get_drop};
    static const GuestFn dev[] = {device_list, device_set_volume, device_get_volume, device_active_name,
                                  device_event, device_channels, device_list, device_set_volume, device_get_volume,
                                  /* 10 */ device_active_name, device_event, device_event, device_active_name,
                                  device_list, NULL, NULL, NULL, NULL, device_set_autotune, device_get_autotune};
    _Static_assert(4 + sizeof mgr / sizeof *mgr == MANAGER_SLOTS, "IAudioRendererManager slots");
    _Static_assert(4 + sizeof ren / sizeof *ren == RENDERER_SLOTS, "IAudioRenderer slots");
    _Static_assert(4 + sizeof dev / sizeof *dev == DEVICE_SLOTS, "IAudioDevice slots");
    manager_vt = vtable(MANAGER_SLOTS, shared_ref, shared_ref, unserved_IAudioRendererManager, mgr, 5);
    renderer_vt = vtable(RENDERER_SLOTS, renderer_add_ref, renderer_release, unserved_IAudioRenderer, ren, 14);
    device_vt = vtable(DEVICE_SLOTS, shared_ref, shared_ref, unserved_IAudioDevice, dev, 20);
    manager = object_new(manager_vt, NULL);
    device = object_new(device_vt, NULL);
}

// nn::audio::CreateAudioRendererManager(bool dfc) -> SharedPointer<IAudioRendererManager> (in x8): the same host
// object for the IPC and the in-process (DFC) choice.
static void create_manager(Cpu *c) {
    plat_once(&once, build, NULL);
    ST64(c->x[8], (uint64_t)(uintptr_t)manager);
}
HOST_OVERRIDE("_ZN2nn5audio26CreateAudioRendererManagerEb", create_manager);
