#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#include "config.h"
#include "la.h"
#include "navigation.h"
#include "screenshot.h"

#ifndef BOOMER_VERSION
#define BOOMER_VERSION "unknown"
#endif

static void quit(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void quit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) quit("Could not read `%s`", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) quit("Could not read `%s`", path);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void create_dir(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

typedef struct {
    const char *path;
    char *content;
} Shader;

#ifdef DEVELOPER
static Shader read_shader(const char *file)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "src/%s", file);
    return (Shader){ .path = strdup(path), .content = read_file(path) };
}

static void reload_shader(Shader *shader)
{
    free(shader->content);
    shader->content = read_file(shader->path);
}
#else
#include "shaders.h"
#endif

static GLuint new_shader(const Shader *shader, GLenum kind)
{
    GLuint result = glCreateShader(kind);
    glShaderSource(result, 1, (const GLchar *const *)&shader->content, NULL);
    glCompileShader(result);

    GLint success;
    char info_log[512];
    glGetShaderiv(result, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(result, sizeof info_log, NULL, info_log);
        printf("------------------------------\n");
        printf("Error during shader compilation: %s. Log:\n", shader->path);
        printf("%s\n", info_log);
        printf("------------------------------\n");
    }
    return result;
}

static GLuint new_shader_program(const Shader *vertex, const Shader *fragment)
{
    GLuint result = glCreateProgram();

    GLuint vs = new_shader(vertex, GL_VERTEX_SHADER);
    GLuint fs = new_shader(fragment, GL_FRAGMENT_SHADER);

    glAttachShader(result, vs);
    glAttachShader(result, fs);

    glBindAttribLocation(result, 0, "aPos");
    glBindAttribLocation(result, 1, "aTexCoord");

    glLinkProgram(result);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success;
    char info_log[512];
    glGetProgramiv(result, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(result, sizeof info_log, NULL, info_log);
        printf("%s\n", info_log);
    }

    glUseProgram(result);
    return result;
}

typedef struct {
    bool is_enabled;
    float shadow;
    float radius;
    float delta_radius;
} Flashlight;

#define INITIAL_FL_DELTA_RADIUS 250.0f
#define FL_DELTA_RADIUS_DECELERATION 10.0f

static void flashlight_update(Flashlight *fl, float dt)
{
    if (fabsf(fl->delta_radius) > 1.0f) {
        float r = fl->radius + fl->delta_radius * dt;
        fl->radius = r > 0.0f ? r : 0.0f;
        fl->delta_radius -= fl->delta_radius * FL_DELTA_RADIUS_DECELERATION * dt;
    }

    if (fl->is_enabled) {
        float s = fl->shadow + 6.0f * dt;
        fl->shadow = s < 0.8f ? s : 0.8f;
    } else {
        float s = fl->shadow - 6.0f * dt;
        fl->shadow = s > 0.0f ? s : 0.0f;
    }
}


static void draw(const XImage *screenshot, const Camera *camera, GLuint shader, GLuint vao,
                 Vec2f window_size, const Mouse *mouse, const Flashlight *fl, bool mirror)
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader);

    glUniform2f(glGetUniformLocation(shader, "cameraPos"), camera->position.x, camera->position.y);
    glUniform1f(glGetUniformLocation(shader, "cameraScale"), camera->scale);
    glUniform2f(glGetUniformLocation(shader, "screenshotSize"),
                (float)screenshot->width, (float)screenshot->height);
    glUniform2f(glGetUniformLocation(shader, "windowSize"), window_size.x, window_size.y);
    glUniform2f(glGetUniformLocation(shader, "cursorPos"), mouse->curr.x, mouse->curr.y);
    glUniform1f(glGetUniformLocation(shader, "flShadow"), fl->shadow);
    glUniform1f(glGetUniformLocation(shader, "flRadius"), fl->radius);
    glUniform1i(glGetUniformLocation(shader, "mirror"), mirror ? 1 : 0);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

static void upload_quad(GLuint vbo, float w, float h)
{
    GLfloat vertices[4][4] = {
        { w,    0.0f, 1.0f, 1.0f }, /* Top right    */
        { w,    h,    1.0f, 0.0f }, /* Bottom right */
        { 0.0f, h,    0.0f, 0.0f }, /* Bottom left  */
        { 0.0f, 0.0f, 0.0f, 1.0f }, /* Top left     */
    };
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof vertices, vertices, GL_STATIC_DRAW);
}

static void upload_texture(const XImage *image)
{
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, image->data);
}

static Vec2f get_cursor_position(Display *display)
{
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(display, DefaultRootWindow(display),
                  &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
    return vec2((float)root_x, (float)root_y);
}

#ifdef SELECT
static Window select_window(Display *display)
{
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);
    Window root = DefaultRootWindow(display);

    XGrabPointer(display, root, False,
                 ButtonMotionMask | ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync,
                 root, cursor, CurrentTime);
    XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);

    Window result = root;
    XEvent event;
    for (;;) {
        XNextEvent(display, &event);
        if (event.type == ButtonPress) {
            result = event.xbutton.subwindow;
            break;
        }
        if (event.type == KeyPress) {
            result = root;
            break;
        }
    }

    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    return result;
}
#endif

static int x11_error_handler(Display *display, XErrorEvent *error_event)
{
    char message[256];
    XGetErrorText(display, error_event->error_code, message, sizeof message);
    printf("X ELEVEN ERROR: %s\n", message);
    return 0;
}

static void scroll(bool up, unsigned int state, Camera *camera, Flashlight *fl,
                   const Mouse *mouse, const Config *config)
{
    double sign = up ? 1.0 : -1.0;
    if ((state & ControlMask) && fl->is_enabled) {
        fl->delta_radius += (float)sign * INITIAL_FL_DELTA_RADIUS;
    } else {
        camera->delta_scale += sign * config->scroll_speed;
        camera->scale_pivot = mouse->curr;
    }
}

static void usage_quit(void)
{
    quit("Usage: boomer [OPTIONS]\n"
         "  -d, --delay <seconds: float>  delay execution of the program by provided <seconds>\n"
         "  -h, --help                    show this help and exit\n"
         "      --new-config [filepath]   generate a new default config at [filepath]\n"
         "  -c, --config <filepath>       use config at <filepath>\n"
         "  -V, --version                 show the current version and exit\n"
         "  -w, --windowed                windowed mode instead of fullscreen");
}

static void version_quit(void)
{
    char hash[9];
    snprintf(hash, sizeof hash, "%s", BOOMER_VERSION);
    quit("boomer-%s", hash);
}

static void get_config_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(out, cap, "%s", xdg);
    } else {
        const char *home = getenv("HOME");
        snprintf(out, cap, "%s/.config", home ? home : "");
    }
}

int main(int argc, char **argv)
{
    char default_config_file[PATH_MAX];
    {
        char dir[PATH_MAX - 32];
        get_config_dir(dir, sizeof dir);
        snprintf(default_config_file, sizeof default_config_file, "%s/boomer/config", dir);
    }
    const char *config_file = default_config_file;
    bool windowed = false;
    double delay_sec = 0.0;

    for (int i = 1; i < argc;) {
        const char *arg = argv[i];

        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--delay") == 0) {
            if (i + 1 >= argc) {
                printf("No value is provided for %s\n", arg);
                usage_quit();
            }
            if (!parse_double(argv[i + 1], &delay_sec))
                quit("Invalid value `%s` for %s", argv[i + 1], arg);
            i += 2;
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--windowed") == 0) {
            windowed = true;
            i += 1;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage_quit();
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            version_quit();
        } else if (strcmp(arg, "--new-config") == 0) {
            const char *new_config_path = config_file;
            if (i + 1 < argc && argv[i + 1][0] != '\0' && argv[i + 1][0] != '-')
                new_config_path = argv[i + 1];

            char dir[PATH_MAX];
            snprintf(dir, sizeof dir, "%s", new_config_path);
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = '\0';
                if (dir[0] != '\0') create_dir(dir);
            }

            if (file_exists(new_config_path)) {
                printf("File %s already exists. Replace it? [yn] ", new_config_path);
                fflush(stdout);
                if (getchar() != 'y') quit("Disaster prevented");
            }

            generate_default_config(new_config_path);
            quit("Generated config at %s", new_config_path);
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) {
                printf("No value is provided for %s\n", arg);
                usage_quit();
            }
            config_file = argv[i + 1];
            i += 2;
        } else {
            printf("Unknown flag `%s`\n", arg);
            usage_quit();
        }
    }

    if (floor(delay_sec * 1000.0) > 0) {
        long ms = (long)floor(delay_sec * 1000.0);
        struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }

    Config config = default_config;

    if (file_exists(config_file)) {
        config = load_config(config_file);
    } else {
        fprintf(stderr, "%s doesn't exist. Using default values. \n", config_file);
    }

    printf("Using config: ");
    print_config(&config);

    Display *display = XOpenDisplay(NULL);
    if (display == NULL) quit("Failed to open display");

    XSetErrorHandler(x11_error_handler);

#ifdef SELECT
    printf("Please select window:\n");
    Window tracking_window = select_window(display);
#else
    Window tracking_window = DefaultRootWindow(display);
#endif

    XRRScreenConfiguration *screen_config = XRRGetScreenInfo(display, DefaultRootWindow(display));
    short rate = XRRConfigCurrentRate(screen_config);
    printf("Screen rate: %d\n", rate);

    int screen = XDefaultScreen(display);
    int glx_major, glx_minor;

    if (!glXQueryVersion(display, &glx_major, &glx_minor) ||
        (glx_major == 1 && glx_minor < 3) ||
        (glx_major < 1)) {
        quit("Invalid GLX version. Expected >=1.3");
    }
    printf("GLX version %d.%d\n", glx_major, glx_minor);
    printf("GLX extension: %s\n", glXQueryExtensionsString(display, screen));

    int attrs[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        None
    };

    XVisualInfo *vi = glXChooseVisual(display, screen, attrs);
    if (vi == NULL) quit("No appropriate visual found");

    printf("Visual %lu selected\n", (unsigned long)vi->visualid);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.colormap = XCreateColormap(display, DefaultRootWindow(display), vi->visual, AllocNone);
    swa.event_mask = ButtonPressMask | ButtonReleaseMask |
                     KeyPressMask | KeyReleaseMask |
                     PointerMotionMask | ExposureMask;
    if (!windowed) {
        swa.override_redirect = True;
        swa.save_under = True;
    }

    XWindowAttributes attributes;
    XGetWindowAttributes(display, DefaultRootWindow(display), &attributes);
    Window win = XCreateWindow(display, DefaultRootWindow(display),
                               0, 0, (unsigned int)attributes.width, (unsigned int)attributes.height, 0,
                               vi->depth, InputOutput, vi->visual,
                               CWColormap | CWEventMask | CWOverrideRedirect | CWSaveUnder, &swa);

    XMapWindow(display, win);

    char wm_name[] = "boomer";
    char wm_class[] = "Boomer";
    XClassHint hints = { .res_name = wm_name, .res_class = wm_class };

    XStoreName(display, win, wm_name);
    XSetClassHint(display, win, &hints);

    Atom wm_delete_message = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, win, &wm_delete_message, 1);

    GLXContext glc = glXCreateContext(display, vi, NULL, GL_TRUE);
    glXMakeCurrent(display, win, glc);

#ifdef DEVELOPER
    Shader vertex_shader = read_shader("vert.glsl");
    Shader fragment_shader = read_shader("frag.glsl");
#else
    const Shader vertex_shader = { .path = "vert.glsl", .content = (char *)vert_glsl };
    const Shader fragment_shader = { .path = "frag.glsl", .content = (char *)frag_glsl };
#endif

    GLuint shader_program = new_shader_program(&vertex_shader, &fragment_shader);

    Screenshot screenshot = screenshot_new(display, tracking_window);

    float w = (float)screenshot.image->width;
    float h = (float)screenshot.image->height;

    GLuint vao, vbo, ebo;
    GLuint indices[] = { 0, 1, 3,
                         1, 2, 3 };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    upload_quad(vbo, w, h);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof indices, indices, GL_STATIC_DRAW);

    GLsizei stride = (GLsizei)(4 * sizeof(GLfloat));

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    upload_texture(screenshot.image);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(glGetUniformLocation(shader_program, "tex"), 0);

    glEnable(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    bool quitting = false;
    Camera camera = { .scale = 1.0f };
    Mouse mouse;
    {
        Vec2f pos = get_cursor_position(display);
        mouse = (Mouse){ .curr = pos, .prev = pos, .drag = false };
    }
    Flashlight flashlight = { .is_enabled = false, .radius = 200.0f };
    bool mirror = false;

    const double dt = 1.0 / (double)rate;

    Window origin_window;
    int revert_to_return;
    XGetInputFocus(display, &origin_window, &revert_to_return);

    while (!quitting) {
        if (!windowed) XSetInputFocus(display, win, RevertToParent, CurrentTime);

        XWindowAttributes wa;
        XGetWindowAttributes(display, win, &wa);
        glViewport(0, 0, wa.width, wa.height);

        XEvent xev;
        while (XPending(display) > 0) {
            XNextEvent(display, &xev);

            switch (xev.type) {
            case Expose:
                break;

            case MotionNotify:
                mouse.curr = vec2((float)xev.xmotion.x, (float)xev.xmotion.y);

                if (mouse.drag) {
                    Vec2f delta = vec2_sub(camera_world(&camera, mouse.prev),
                                           camera_world(&camera, mouse.curr));
                    camera.position = vec2_add(camera.position, delta);
                    camera.velocity = vec2_scale(delta, (float)rate);
                }

                mouse.prev = mouse.curr;
                break;

            case ClientMessage:
                if ((Atom)xev.xclient.data.l[0] == wm_delete_message) quitting = true;
                break;

            case KeyPress: {
                KeySym key = XLookupKeysym(&xev.xkey, 0);
                switch (key) {
                case XK_equal:
                    scroll(true, xev.xkey.state, &camera, &flashlight, &mouse, &config);
                    break;
                case XK_minus:
                    scroll(false, xev.xkey.state, &camera, &flashlight, &mouse, &config);
                    break;
                case XK_0:
                    camera.scale = 1.0f;
                    camera.delta_scale = 0.0;
                    camera.position = vec2(0.0f, 0.0f);
                    camera.velocity = vec2(0.0f, 0.0f);
                    mirror = false;
                    break;
                case XK_q:
                case XK_Escape:
                    quitting = true;
                    break;
                case XK_r:
                    if (config_file[0] != '\0' && file_exists(config_file))
                        config = load_config(config_file);

#ifdef DEVELOPER
                    if (xev.xkey.state & ControlMask) {
                        printf("------------------------------\n");
                        printf("RELOADING SHADERS\n");
                        reload_shader(&vertex_shader);
                        reload_shader(&fragment_shader);
                        GLuint new_program = new_shader_program(&vertex_shader, &fragment_shader);
                        glDeleteProgram(shader_program);
                        shader_program = new_program;
                        printf("Shader program ID: %u\n", shader_program);
                        printf("------------------------------\n");
                    }
#endif
                    break;
                case XK_m:
                    camera.position.x += (float)screenshot.image->width / camera.scale
                                       - 2.0f * (mouse.curr.x / camera.scale + camera.position.x);
                    mirror = !mirror;
                    break;
                case XK_f:
                    flashlight.is_enabled = !flashlight.is_enabled;
                    break;
                default:
                    break;
                }
                break;
            }

            case ButtonPress:
                switch (xev.xbutton.button) {
                case Button1:
                    mouse.prev = mouse.curr;
                    mouse.drag = true;
                    camera.velocity = vec2(0.0f, 0.0f);
                    break;
                case Button4:
                    scroll(true, xev.xkey.state, &camera, &flashlight, &mouse, &config);
                    break;
                case Button5:
                    scroll(false, xev.xkey.state, &camera, &flashlight, &mouse, &config);
                    break;
                default:
                    break;
                }
                break;

            case ButtonRelease:
                if (xev.xbutton.button == Button1) mouse.drag = false;
                break;

            default:
                break;
            }
        }

        Vec2f window_size = vec2((float)wa.width, (float)wa.height);

        camera_update(&camera, &config, dt, &mouse, window_size);
        flashlight_update(&flashlight, (float)dt);

        draw(screenshot.image, &camera, shader_program, vao, window_size, &mouse, &flashlight, mirror);

        glXSwapBuffers(display, win);
        glFinish();

#ifdef LIVE
        screenshot_refresh(&screenshot, display, tracking_window);
        upload_quad(vbo, (float)screenshot.image->width, (float)screenshot.image->height);
        upload_texture(screenshot.image);
#endif
    }

    XSetInputFocus(display, origin_window, RevertToParent, CurrentTime);
    XSync(display, False);

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    screenshot_destroy(&screenshot, display);
    XCloseDisplay(display);

    return 0;
}
