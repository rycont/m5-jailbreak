typedef void *EGLDisplay;
typedef void *EGLDeviceEXT;
typedef long EGLAttrib;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLint;
typedef unsigned long long EGLuint64KHR;
typedef const char *(*EGLQueryStringFn)(EGLDisplay, int);
typedef unsigned int (*EGLQueryDisplayAttribFn)(EGLDisplay, int, EGLAttrib *);
typedef const char *(*EGLQueryDeviceStringFn)(EGLDeviceEXT, int);
typedef void *(*EGLGetProcAddressFn)(const char *);

extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern int strcmp(const char *, const char *);

#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100

static void *egl_handle;
static EGLQueryStringFn real_query_string;
static EGLGetProcAddressFn real_get_proc_address;
static char extension_buffer[8192];

static EGLQueryStringFn get_real_query_string(void)
{
    if (!real_query_string) {
        if (!egl_handle)
            egl_handle = dlopen("/tmp/eglw2/libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (egl_handle)
            real_query_string = (EGLQueryStringFn)dlsym(egl_handle, "eglQueryString");
    }
    return real_query_string;
}

static unsigned int append_text(char *dst, unsigned int at, const char *src)
{
    if (!src)
        return at;
    while (*src && at < sizeof(extension_buffer) - 1)
        dst[at++] = *src++;
    return at;
}

static const char *query_string_impl(EGLDisplay display, int name)
{
    EGLQueryStringFn query = get_real_query_string();
    const char *actual = query ? query(display, name) : 0;
    if (name != 0x3055)
        return actual;

    unsigned int n = 0;
    if (!display) {
        n = append_text(extension_buffer, n,
            "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_KHR_platform_gbm "
            "EGL_KHR_platform_wayland EGL_EXT_device_query EGL_EXT_device_base "
            "EGL_EXT_device_drm EGL_EXT_device_drm_render_node "
            "EGL_KHR_no_config_context EGL_KHR_surfaceless_context "
            "EGL_EXT_image_dma_buf_import EGL_EXT_image_dma_buf_import_modifiers");
        if (actual && *actual) {
            extension_buffer[n++] = ' ';
            n = append_text(extension_buffer, n, actual);
        }
    } else {
        n = append_text(extension_buffer, n, actual);
        n = append_text(extension_buffer, n,
            " EGL_EXT_device_query EGL_EXT_device_base EGL_EXT_device_drm "
            "EGL_EXT_device_drm_render_node EGL_KHR_no_config_context "
            "EGL_KHR_surfaceless_context EGL_EXT_image_dma_buf_import "
            "EGL_EXT_image_dma_buf_import_modifiers");
    }
    extension_buffer[n] = 0;
    return extension_buffer;
}

static unsigned int query_display_attrib_impl(EGLDisplay display, int name,
                                               EGLAttrib *value)
{
    (void)display;
    if (name == 0x322C) {
        if (value)
            *value = 1;
        return 1;
    }
    return 0;
}

static const char *query_device_string_impl(EGLDeviceEXT device, int name)
{
    (void)device;
    if (name == 0x3055)
        return "EGL_EXT_device_drm_render_node EGL_EXT_device_drm";
    if (name == 0x3377 || name == 0x3233)
        return "/dev/dri/card0";
    return 0;
}

static EGLBoolean query_dma_buf_formats_impl(EGLDisplay display, EGLint max_formats,
                                              EGLint *formats, EGLint *num_formats)
{
    (void)display;
    if (!num_formats)
        return 0;
    const EGLint known[] = {0x34325258, 0x34325241}; /* XR24, AR24 */
    if (!formats) {
        *num_formats = 2;
        return 1;
    }
    EGLint count = max_formats < 2 ? max_formats : 2;
    for (EGLint i = 0; i < count; ++i)
        formats[i] = known[i];
    *num_formats = count;
    return 1;
}

static EGLBoolean query_dma_buf_modifiers_impl(EGLDisplay display, EGLint format,
                                                EGLint max_modifiers,
                                                EGLuint64KHR *modifiers,
                                                EGLBoolean *external_only,
                                                EGLint *num_modifiers)
{
    (void)display;
    if (!num_modifiers || (format != 0x34325258 && format != 0x34325241))
        return 0;
    if (external_only && max_modifiers > 0)
        external_only[0] = 0;
    if (modifiers && max_modifiers > 0)
        modifiers[0] = 0x00ffffffffffffffULL; /* implicit modifier */
    *num_modifiers = 1;
    return 1;
}

static void *query_proc_address_impl(const char *name)
{
    if (!name)
        return 0;
    if (strcmp(name, "eglQueryDmaBufFormatsEXT") == 0)
        return (void *)query_dma_buf_formats_impl;
    if (strcmp(name, "eglQueryDmaBufModifiersEXT") == 0)
        return (void *)query_dma_buf_modifiers_impl;
    if (!egl_handle)
        egl_handle = dlopen("/tmp/eglw2/libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (egl_handle && !real_get_proc_address)
        real_get_proc_address = (EGLGetProcAddressFn)dlsym(egl_handle, "eglGetProcAddress");
    return real_get_proc_address ? real_get_proc_address(name) : 0;
}

/* libepoxy exports these EGL dispatch entries as data pointers, not functions. */
EGLQueryStringFn epoxy_eglQueryString = query_string_impl;
EGLQueryDisplayAttribFn epoxy_eglQueryDisplayAttribEXT = query_display_attrib_impl;
EGLQueryDeviceStringFn epoxy_eglQueryDeviceStringEXT = query_device_string_impl;
EGLGetProcAddressFn epoxy_eglGetProcAddress = query_proc_address_impl;
