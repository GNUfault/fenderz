/*
 * fenderz - My old random physics engine (renderz) revived
 * Copyright (C) 2025 Connor Thomson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

const float GRAVITY = 9.81f;
const float GROUND_Y = -2.0f;
const float CUBE_SIZE = 0.5f;
const int NUM_CUBES = 100;
const float BOUNCE_FACTOR = 1.0f;
const float FRICTION_FACTOR = 0.9f;
const float REST_THRESHOLD = 0.05f;
const float RESET_INTERVAL_SECONDS = 10.0f;
const float AUTO_ROTATE_SPEED_Y = 100.0f;
const float CAMERA_HEIGHT_OFFSET = 8.0f;

const bool DEBUG_MODE = false;

Display* g_display = NULL;
Window g_window;
GLXContext g_glContext;
Colormap g_colorMap;
GLuint g_cube_texture_id;

Cursor g_invisibleCursor;

float rotateX = 0.0f;
float rotateY = 0.0f;

struct timeval lastFrameTime_tv;

float secondTimer = 0.0f;
int secondsCount = 0;

float fpsTimer = 0.0f;
int frameCount = 0;

float resetTimer = 0.0f;

PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI = NULL;
PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA = NULL;

typedef struct {
    float x, y, z;
} Vec3;

Vec3 vec3_create(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3_create(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3_create(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 vec3_mul_scalar(Vec3 v, float scalar) {
    return vec3_create(v.x * scalar, v.y * scalar, v.z * scalar);
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3_create(a.y * b.z - a.z * b.y,
                        a.z * b.x - a.x * b.z,
                        a.x * b.y - a.y * b.x);
}

float vec3_length(Vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > 0) return vec3_create(v.x / len, v.y / len, v.z / len);
    return vec3_create(0.0f, 0.0f, 0.0f);
}

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 angularVelocity;
    Vec3 rotation;
    Vec3 color;
    float size;
    bool resting;
} Cube;

Cube* g_cubes = NULL;

float rand_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

void initX11OpenGL();
void destroyX11OpenGL();
void handleXEvents(XEvent* event, bool* quitFlag);
void initOpenGL();
void loadCubeTexture();
void display();
void reshape(int width, int height);
void resetCubes();
void updatePhysics(float deltaTime);
void drawCube(const Vec3* position, const Vec3* rotation, const Vec3* color, float size);

int main(int argc, char** argv) {
    bool bQuit = false;

    srand(time(NULL));

    initX11OpenGL();

    initOpenGL();

    gettimeofday(&lastFrameTime_tv, NULL);

    XEvent event;
    while (!bQuit) {
        while (XPending(g_display) > 0) {
            XNextEvent(g_display, &event);
            handleXEvents(&event, &bQuit);
            if (bQuit) break;
        }
        if (bQuit) break;

        struct timeval currentTime_tv;
        gettimeofday(&currentTime_tv, NULL);
        float deltaTime = (float)(currentTime_tv.tv_sec - lastFrameTime_tv.tv_sec) +
                          (float)(currentTime_tv.tv_usec - lastFrameTime_tv.tv_usec) / 1000000.0f;
        lastFrameTime_tv = currentTime_tv;

        updatePhysics(deltaTime);

        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 0.5f) {
            float currentFps = (float)frameCount / fpsTimer;
            if (DEBUG_MODE) {
                printf("FPS: %.2f\n", currentFps);
            }
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        display();
        glXSwapBuffers(g_display, g_window);
    }

    destroyX11OpenGL();

    return 0;
}

void handleXEvents(XEvent* event, bool* quitFlag) {
    switch (event->type) {
        case Expose:
            break;
        case ConfigureNotify:
            reshape(event->xconfigure.width, event->xconfigure.height);
            break;
        case KeyPress:
            *quitFlag = true;
            break;
        case ClientMessage:
            if (strcmp(XGetAtomName(g_display, event->xclient.message_type), "WM_PROTOCOLS") == 0) {
                Atom wmDelete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
                if (event->xclient.data.l[0] == (long)wmDelete) {
                    *quitFlag = true;
                }
            }
            break;
    }
}

void initX11OpenGL() {
    g_display = XOpenDisplay(NULL);
    if (g_display == NULL) {
        fprintf(stderr, "Error: Could not open X display.\n");
        exit(1);
    }

    int screen = DefaultScreen(g_display);
    Window root = RootWindow(g_display, screen);

    int screenWidth = XDisplayWidth(g_display, screen);
    int screenHeight = XDisplayHeight(g_display, screen);

    GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
    XVisualInfo* vi = glXChooseVisual(g_display, screen, att);
    if (vi == NULL) {
        fprintf(stderr, "Error: No appropriate visual found.\n");
        exit(1);
    }

    g_colorMap = XCreateColormap(g_display, root, vi->visual, AllocNone);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = g_colorMap;
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    g_window = XCreateWindow(g_display, root, 0, 0, screenWidth, screenHeight, 0,
                             vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);

    XStoreName(g_display, g_window, "fenderz - Physics Engine");

    XSizeHints* size_hints = XAllocSizeHints();
    if (size_hints) {
        size_hints->flags = PMinSize | PMaxSize;
        size_hints->min_width = screenWidth;
        size_hints->max_width = screenWidth;
        size_hints->min_height = screenHeight;
        size_hints->max_height = screenHeight;
        XSetWMNormalHints(g_display, g_window, size_hints);
        XFree(size_hints);
    }

    XMapWindow(g_display, g_window);

    Atom wm_state = XInternAtom(g_display, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(g_display, "_NET_WM_STATE_FULLSCREEN", False);

    if (wm_state != None && fullscreen != None) {
        XEvent xev;
        memset(&xev, 0, sizeof(xev));
        xev.type = ClientMessage;
        xev.xclient.window = g_window;
        xev.xclient.message_type = wm_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 1;
        xev.xclient.data.l[1] = fullscreen;
        xev.xclient.data.l[2] = 0;
        xev.xclient.data.l[3] = 0;
        xev.xclient.data.l[4] = 0;

        XSendEvent(g_display, root, True,
                   SubstructureNotifyMask | SubstructureRedirectMask, &xev);
        XFlush(g_display);
        XSync(g_display, False);
    }

    g_glContext = glXCreateContext(g_display, vi, NULL, GL_TRUE);
    if (g_glContext == NULL) {
        fprintf(stderr, "Error: Failed to create GLX context.\n");
        exit(1);
    }

    glXMakeCurrent(g_display, g_window, g_glContext);

    glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress((const GLubyte*)"glXSwapIntervalSGI");
    glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress((const GLubyte*)"glXSwapIntervalMESA");

    int swap_interval = DEBUG_MODE ? 0 : 1;

    if (glXSwapIntervalSGI) {
        glXSwapIntervalSGI(swap_interval);
        if (DEBUG_MODE) {
            printf("V-Sync disabled (DEBUG_MODE is true) using glXSwapIntervalSGI.\n");
        } else {
            printf("V-Sync enabled (DEBUG_MODE is false) using glXSwapIntervalSGI.\n");
        }
    } else if (glXSwapIntervalMESA) {
        glXSwapIntervalMESA(swap_interval);
           if (DEBUG_MODE) {
            printf("V-Sync disabled (DEBUG_MODE is true) using glXSwapIntervalMESA.\n");
        } else {
            printf("V-Sync enabled (DEBUG_MODE is false) using glXSwapIntervalMESA.\n");
        }
    } else {
        if (DEBUG_MODE) {
            printf("Could not control V-Sync (neither glXSwapIntervalSGI nor glXSwapIntervalMESA found or supported).\n");
        }
    }

    XFree(vi);

    reshape(screenWidth, screenHeight);

    Pixmap pixmap;
    XColor color;
    char data = 0;
    pixmap = XCreateBitmapFromData(g_display, g_window, &data, 1, 1);
    color.red = color.green = color.blue = 0;
    g_invisibleCursor = XCreatePixmapCursor(g_display, pixmap, pixmap, &color, &color, 0, 0);
    XFreePixmap(g_display, pixmap);

    XGrabPointer(g_display, g_window, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, g_window, g_invisibleCursor, CurrentTime);

    if (DEBUG_MODE) {
        printf("X11 window and OpenGL context initialized. Mouse cursor hidden.\n");
    }
}

void destroyX11OpenGL() {
    if (g_display) {
        XUngrabPointer(g_display, CurrentTime);
        XFreeCursor(g_display, g_invisibleCursor);

        Atom wm_state = XInternAtom(g_display, "_NET_WM_STATE", False);
        Atom fullscreen = XInternAtom(g_display, "_NET_WM_STATE_FULLSCREEN", False);

        if (wm_state != None && fullscreen != None) {
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = ClientMessage;
            xev.xclient.window = g_window;
            xev.xclient.message_type = wm_state;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] = 0;
            xev.xclient.data.l[1] = fullscreen;
            xev.xclient.data.l[2] = 0;
            xev.xclient.data.l[3] = 0;
            xev.xclient.data.l[4] = 0;

            XSendEvent(g_display, RootWindow(g_display, DefaultScreen(g_display)), False,
                       SubstructureNotifyMask | SubstructureRedirectMask, &xev);
            XFlush(g_display);
        }

        glXMakeCurrent(g_display, None, NULL);
        if (g_glContext) {
            glXDestroyContext(g_display, g_glContext);
        }
        XDestroyWindow(g_display, g_window);
        XFreeColormap(g_display, g_colorMap);
        XCloseDisplay(g_display);
        g_display = NULL;
        g_glContext = NULL;
        if (DEBUG_MODE) {
            printf("X11 window and OpenGL context destroyed. Mouse cursor unhidden.\n");
        }
    }
    if (g_cubes != NULL) {
        free(g_cubes);
        g_cubes = NULL;
    }

    if (g_cube_texture_id != 0) {
        glDeleteTextures(1, &g_cube_texture_id);
    }
}

void loadCubeTexture() {

    unsigned char texture_data[] = {
        255, 255, 255,    0,    0,    0, 255, 255, 255,    0,    0,    0,
          0,    0,    0, 255, 255, 255,    0,    0,    0, 255, 255, 255,
        255, 255, 255,    0,    0,    0, 255, 255, 255,    0,    0,    0,
          0,    0,    0, 255, 255, 255,    0,    0,    0, 255, 255, 255
    };
    int texture_width = 4;
    int texture_height = 4;

    glGenTextures(1, &g_cube_texture_id);
    glBindTexture(GL_TEXTURE_2D, g_cube_texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_width, texture_height, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);

    if (DEBUG_MODE) {
        printf("Cube texture loaded.\n");
    }
}

void initOpenGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (DEBUG_MODE) {
        printf("Sky color set to black at initialization.\n");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_TEXTURE_2D);

    GLfloat light_position[] = {1.0f, 1.0f, 1.0f, 0.0f};
    GLfloat light_ambient[] = {0.2f, 0.2f, 0.2f, 1.0f};
    GLfloat light_diffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
    GLfloat light_specular[] = {1.0f, 1.0f, 1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    loadCubeTexture();

    resetCubes();
}

void resetCubes() {
    if (g_cubes != NULL) {
        free(g_cubes);
    }
    g_cubes = (Cube*)malloc(sizeof(Cube) * NUM_CUBES);
    if (g_cubes == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for cubes.\n");
        exit(1);
    }

    for (int i = 0; i < NUM_CUBES; ++i) {
        Cube* newCube = &g_cubes[i];
        newCube->size = CUBE_SIZE;
        newCube->velocity = vec3_create(0.0f, 0.0f, 0.0f);
        newCube->angularVelocity = vec3_create(0.0f, 0.0f, 0.0f);
        newCube->rotation = vec3_create(0.0f, 0.0f, 0.0f);
        newCube->resting = false;
        newCube->color = vec3_create(rand_float(0.0f, 1.0f), rand_float(0.0f, 1.0f), rand_float(0.0f, 1.0f));

        float x_offset = (i % 10 - 5.0f) * (CUBE_SIZE * 2.0f);
        float z_offset = ((i / 10) % 10 - 5.0f) * (CUBE_SIZE * 2.0f);
        float y_offset = (i / 100) * (CUBE_SIZE * 2.0f) + rand_float(5.0f, 15.0f);

        newCube->position = vec3_create(x_offset, y_offset, z_offset);
    }
    secondTimer = 0.0f;
    secondsCount = 0;
    fpsTimer = 0.0f;
    frameCount = 0;
    resetTimer = 0.0f;
    if (DEBUG_MODE) {
        printf("Cubes reset.\n");
    }
}

void updatePhysics(float deltaTime) {
    secondTimer += deltaTime;
    if (secondTimer >= 1.0f) {
        secondsCount++;
        if (DEBUG_MODE) {
            printf("Seconds: %d\n", secondsCount);
        }
        secondTimer = 0.0f;
    }

    resetTimer += deltaTime;
    if (resetTimer >= RESET_INTERVAL_SECONDS) {
        if (DEBUG_MODE) {
            printf("Resetting cubes due to timer.\n");
        }
        resetCubes();
    }

    rotateY += AUTO_ROTATE_SPEED_Y * deltaTime;
    rotateY = fmodf(rotateY, 360.0f);

    for (int i = 0; i < NUM_CUBES; ++i) {
        Cube* cube = &g_cubes[i];

        cube->velocity.y -= GRAVITY * deltaTime;

        cube->position.x += cube->velocity.x * deltaTime;
        cube->position.y += cube->velocity.y * deltaTime;
        cube->position.z += cube->velocity.z * deltaTime;

        cube->rotation.x += cube->angularVelocity.x * deltaTime;
        cube->rotation.y += cube->angularVelocity.y * deltaTime;
        cube->rotation.z += cube->angularVelocity.z * deltaTime;

        cube->rotation.x = fmodf(cube->rotation.x, 360.0f);
        cube->rotation.y = fmodf(cube->rotation.y, 360.0f);
        cube->rotation.z = fmodf(cube->rotation.z, 360.0f);

        float halfSize = cube->size / 2.0f;
        float cube_bottom = cube->position.y - halfSize;

        if (cube_bottom < GROUND_Y) {
            cube->position.y = GROUND_Y + halfSize;

            Vec3 normal = vec3_create(0.0f, 1.0f, 0.0f);
            Vec3 random_perturb = vec3_create(rand_float(-0.5f, 0.5f), 0.0f, rand_float(-0.5f, 0.5f));
            Vec3 bounce_direction = vec3_normalize(vec3_add(normal, random_perturb));

            float normal_speed = vec3_dot(cube->velocity, normal);
            Vec3 new_normal_velocity = vec3_mul_scalar(bounce_direction, (-normal_speed * BOUNCE_FACTOR));

            Vec3 tangential_velocity = vec3_sub(cube->velocity, vec3_mul_scalar(normal, normal_speed));
            tangential_velocity = vec3_mul_scalar(tangential_velocity, FRICTION_FACTOR);

            cube->velocity = vec3_add(new_normal_velocity, tangential_velocity);

            if (fabsf(normal_speed) > REST_THRESHOLD) {
                cube->angularVelocity = vec3_create(rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f));
            }
        }

        float bound = 8.0f;
        if (cube->position.x - halfSize < -bound) {
            cube->position.x = -bound + halfSize;
            Vec3 normal = vec3_create(1.0f, 0.0f, 0.0f);
            Vec3 random_perturb = vec3_create(0.0f, rand_float(-0.5f, 0.5f), rand_float(-0.5f, 0.5f));
            Vec3 bounce_direction = vec3_normalize(vec3_add(normal, random_perturb));

            float normal_speed = vec3_dot(cube->velocity, normal);
            Vec3 new_normal_velocity = vec3_mul_scalar(bounce_direction, (-normal_speed * BOUNCE_FACTOR));
            Vec3 tangential_velocity = vec3_sub(cube->velocity, vec3_mul_scalar(normal, normal_speed));
            tangential_velocity = vec3_mul_scalar(tangential_velocity, FRICTION_FACTOR);
            cube->velocity = vec3_add(new_normal_velocity, tangential_velocity);

            if (fabsf(normal_speed) > REST_THRESHOLD) {
                cube->angularVelocity = vec3_create(rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f));
            }
        } else if (cube->position.x + halfSize > bound) {
            cube->position.x = bound - halfSize;
            Vec3 normal = vec3_create(-1.0f, 0.0f, 0.0f);
            Vec3 random_perturb = vec3_create(0.0f, rand_float(-0.5f, 0.5f), rand_float(-0.5f, 0.5f));
            Vec3 bounce_direction = vec3_normalize(vec3_add(normal, random_perturb));

            float normal_speed = vec3_dot(cube->velocity, normal);
            Vec3 new_normal_velocity = vec3_mul_scalar(bounce_direction, (-normal_speed * BOUNCE_FACTOR));
            Vec3 tangential_velocity = vec3_sub(cube->velocity, vec3_mul_scalar(normal, normal_speed));
            tangential_velocity = vec3_mul_scalar(tangential_velocity, FRICTION_FACTOR);
            cube->velocity = vec3_add(new_normal_velocity, tangential_velocity);

            if (fabsf(normal_speed) > REST_THRESHOLD) {
                cube->angularVelocity = vec3_create(rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f));
            }
        }

        if (cube->position.z - halfSize < -bound) {
            cube->position.z = -bound + halfSize;
            Vec3 normal = vec3_create(0.0f, 0.0f, 1.0f);
            Vec3 random_perturb = vec3_create(rand_float(-0.5f, 0.5f), rand_float(-0.5f, 0.5f), 0.0f);
            Vec3 bounce_direction = vec3_normalize(vec3_add(normal, random_perturb));

            float normal_speed = vec3_dot(cube->velocity, normal);
            Vec3 new_normal_velocity = vec3_mul_scalar(bounce_direction, (-normal_speed * BOUNCE_FACTOR));
            Vec3 tangential_velocity = vec3_sub(cube->velocity, vec3_mul_scalar(normal, normal_speed));
            tangential_velocity = vec3_mul_scalar(tangential_velocity, FRICTION_FACTOR);
            cube->velocity = vec3_add(new_normal_velocity, tangential_velocity);

            if (fabsf(normal_speed) > REST_THRESHOLD) {
                cube->angularVelocity = vec3_create(rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f));
            }
        } else if (cube->position.z + halfSize > bound) {
            cube->position.z = bound - halfSize;
            Vec3 normal = vec3_create(0.0f, 0.0f, -1.0f);
            Vec3 random_perturb = vec3_create(rand_float(-0.5f, 0.5f), rand_float(-0.5f, 0.5f), 0.0f);
            Vec3 bounce_direction = vec3_normalize(vec3_add(normal, random_perturb));

            float normal_speed = vec3_dot(cube->velocity, normal);
            Vec3 new_normal_velocity = vec3_mul_scalar(bounce_direction, (-normal_speed * BOUNCE_FACTOR));
            Vec3 tangential_velocity = vec3_sub(cube->velocity, vec3_mul_scalar(normal, normal_speed));
            tangential_velocity = vec3_mul_scalar(tangential_velocity, FRICTION_FACTOR);
            cube->velocity = vec3_add(new_normal_velocity, tangential_velocity);

            if (fabsf(normal_speed) > REST_THRESHOLD) {
                cube->angularVelocity = vec3_create(rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f), rand_float(-180.0f, 180.0f));
            }
        }

        if (vec3_length(cube->velocity) < REST_THRESHOLD && vec3_length(cube->angularVelocity) < REST_THRESHOLD * 10 && (cube_bottom <= GROUND_Y + REST_THRESHOLD)) {
            cube->resting = true;
            cube->velocity = vec3_create(0.0f, 0.0f, 0.0f);
            cube->angularVelocity = vec3_create(0.0f, 0.0f, 0.0f);
        } else {
            cube->resting = false;
        }
    }

}

void drawCube(const Vec3* position, const Vec3* rotation, const Vec3* color, float size) {
    glPushMatrix();

    glTranslatef(position->x, position->y, position->z);

    glRotatef(rotation->x, 1.0f, 0.0f, 0.0f);
    glRotatef(rotation->y, 0.0f, 1.0f, 0.0f);
    glRotatef(rotation->z, 0.0f, 0.0f, 1.0f);

    glScalef(size / 2.0f, size / 2.0f, size / 2.0f);

    glBindTexture(GL_TEXTURE_2D, g_cube_texture_id);

    glColor3f(color->x, color->y, color->z);

    glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glNormal3f(0.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glNormal3f(1.0f, 0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    glEnd();

    glBegin(GL_QUADS);
        glNormal3f(-1.0f, 0.0f, 0.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glEnd();

    glPopMatrix();
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    gluLookAt(0.0, 0.0 + CAMERA_HEIGHT_OFFSET, 15.0,
              0.0, 0.0, 0.0,
              0.0, 1.0, 0.0);

    glRotatef(rotateX, 1.0f, 0.0f, 0.0f);
    glRotatef(rotateY, 0.0f, 1.0f, 0.0f);

    for (int i = 0; i < NUM_CUBES; ++i) {
        drawCube(&g_cubes[i].position, &g_cubes[i].rotation, &g_cubes[i].color, g_cubes[i].size);
    }
}

void reshape(int width, int height) {
    if (height == 0) height = 1;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)width / (GLfloat)height, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
