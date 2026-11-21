// Amherst Run (native) - phase 0 toolchain test: an SDL2 window with an OpenGL ES 3.0 context, the 320x180
// picture scaled up with chunky pixels, and a box you drive around a test yard with a pad (or the keyboard).
// It proves the build and the controls on every target before the real map arrives (amherst-run: docs/CPP_PORT_PLAN.md, phase 1).
//
//   amherst-run                         play (pad: left stick steer, RT gas, LT brake/reverse, Back quits)
//   amherst-run --frames 120 --screenshot out.bmp   drive itself for 120 frames, save the picture, exit (CI)
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/input.h"
#include "core/math.h"
#include "render/gl.h"
#include "render/shader.h"
#include "render/target.h"

#ifndef AMR_VERSION
#define AMR_VERSION "dev"
#endif

namespace {

constexpr int kInternalW = 320, kInternalH = 180;  // the Python game's game/config.py INTERNAL_W/H
constexpr double kPhysicsHz = 60.0;                 // fixed timestep, as PHYSICS_HZ

const char* kVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec3 v_normal;
out vec3 v_world;
void main() {
    vec4 w = u_model * vec4(a_pos, 1.0);
    v_world = w.xyz;
    v_normal = mat3(u_model) * a_normal;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

// Two-tone cel lighting and banded distance fog: a first taste of the street style (phase 4 adds ink outlines).
const char* kFragment = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
in vec3 v_world;
uniform vec3 u_eye;
uniform vec3 u_color;
uniform vec3 u_fog;
uniform int u_checker;
out vec4 o_color;
void main() {
    vec3 c = u_color;
    if (u_checker == 1) {                       // the yard: 10 m squares so you can see yourself move
        float k = mod(floor(v_world.x / 10.0) + floor(v_world.y / 10.0), 2.0);
        c *= mix(1.0, 0.88, k);
    }
    float lit = dot(normalize(v_normal), normalize(vec3(0.45, -0.3, 0.85))) > 0.35 ? 1.0 : 0.62;
    float dist = distance(v_world, u_eye);      // per pixel: big polygons (the ground) have far-away corners
    float fog = floor(clamp((dist - 70.0) / 130.0, 0.0, 1.0) * 4.0) / 4.0;   // four bands, like the retro look
    o_color = vec4(mix(c * lit, u_fog, fog), 1.0);
}
)";

// A unit box (-0.5..0.5 in x and y, 0..1 in z): 6 faces, 4 vertices each (flat normals), position + normal.
struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
};

Mesh make_box() {
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float v[6 * 4 * 6];
    unsigned short idx[6 * 6];
    for (int f = 0; f < 6; ++f) {
        // two axes across the face
        float u[3] = {0, 0, 0}, w[3] = {0, 0, 0};
        int ax = n[f][0] != 0 ? 0 : (n[f][1] != 0 ? 1 : 2);
        u[(ax + 1) % 3] = 1;
        w[(ax + 2) % 3] = 1;
        const float su[4] = {-1, 1, 1, -1}, sw[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; ++k) {
            float p[3];
            for (int i = 0; i < 3; ++i) p[i] = 0.5f * (n[f][i] + su[k] * u[i] + sw[k] * w[i]);
            p[2] += 0.5f;  // sit on the ground
            float* o = &v[(f * 4 + k) * 6];
            o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
            o[3] = n[f][0]; o[4] = n[f][1]; o[5] = n[f][2];
        }
        // counter-clockwise seen from outside (front faces); flip the corner order where the face would point in
        const float* c0 = &v[(f * 4 + 0) * 6];
        const float* c1 = &v[(f * 4 + 1) * 6];
        const float* c2 = &v[(f * 4 + 2) * 6];
        const amr::Vec3 tri = amr::cross({c1[0] - c0[0], c1[1] - c0[1], c1[2] - c0[2]},
                                         {c2[0] - c0[0], c2[1] - c0[1], c2[2] - c0[2]});
        const bool outward = amr::dot(tri, {n[f][0], n[f][1], n[f][2]}) > 0;
        const unsigned short base = static_cast<unsigned short>(f * 4);
        const unsigned short ccw[6] = {0, 1, 2, 0, 2, 3}, cw[6] = {0, 2, 1, 0, 3, 2};
        for (int i = 0; i < 6; ++i) idx[f * 6 + i] = static_cast<unsigned short>(base + (outward ? ccw[i] : cw[i]));
    }
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    m.count = 36;
    return m;
}

// A placeholder car: just enough to feel the pad. Phase 1 ports the real arcade physics (game/car.py).
struct TestCar {
    float x = 0, y = 0, heading = 0, speed = 0;
    void step(const amr::Controls& c, float dt) {
        const float accel = 9.0f, brake = 14.0f, drag = 0.35f, max_rev = -6.0f;
        speed += c.throttle * accel * dt;
        speed -= c.brake * (speed > 0 ? brake : accel * 0.5f) * dt;
        speed -= speed * drag * dt;
        if (speed < max_rev) speed = max_rev;
        float grip = std::fmin(1.0f, std::fabs(speed) / 6.0f);           // no turning on the spot
        heading -= c.steer * 1.9f * grip * dt * (speed >= 0 ? 1.0f : -1.0f);
        x += std::cos(heading) * speed * dt;
        y += std::sin(heading) * speed * dt;
    }
};

struct Args {
    int frames = -1;
    std::string screenshot;
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("Amherst Run (native) %s\n", AMR_VERSION);
            std::exit(0);
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            a.frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            a.screenshot = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse(argc, argv, args)) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* win = SDL_CreateWindow("Amherst Run", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       kInternalW * 3, kInternalH * 3, flags);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "OpenGL ES 3.0 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    std::printf("Amherst Run (native) %s: %s\n", AMR_VERSION, reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    std::string err;
    GLuint prog = amr::make_program(kVertex, kFragment, err);
    if (!prog) {
        std::fprintf(stderr, "shader: %s\n", err.c_str());
        return 1;
    }
    const GLint u_mvp = glGetUniformLocation(prog, "u_mvp"), u_model = glGetUniformLocation(prog, "u_model");
    const GLint u_eye = glGetUniformLocation(prog, "u_eye"), u_color = glGetUniformLocation(prog, "u_color");
    const GLint u_fog = glGetUniformLocation(prog, "u_fog"), u_checker = glGetUniformLocation(prog, "u_checker");
    amr::RenderTarget target(kInternalW, kInternalH);
    if (!target.ok()) {
        std::fprintf(stderr, "could not make the 320x180 render target\n");
        return 1;
    }
    Mesh box = make_box();
    amr::Input input;
    TestCar car;
    const amr::Vec3 sky{0.47f, 0.66f, 0.86f};

    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double acc = 0.0;
    int frame = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) input.handle(e);
        amr::Controls c = input.poll();
        if (c.quit) break;
        if (args.frames >= 0) c = amr::Controls{0.35f, 1.0f, 0.0f, false, false};   // drive itself (CI)

        // fixed-timestep simulation; a frame-limited test run steps exactly one 30 fps frame each loop
        Uint64 now = SDL_GetPerformanceCounter();
        acc += args.frames >= 0 ? 1.0 / 30.0 : std::fmin(0.25, (now - last) / freq);
        last = now;
        while (acc >= 1.0 / kPhysicsHz) {
            car.step(c, static_cast<float>(1.0 / kPhysicsHz));
            acc -= 1.0 / kPhysicsHz;
        }

        // draw the small picture
        target.bind();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glClearColor(sky.x, sky.y, sky.z, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const amr::Vec3 eye{car.x - std::cos(car.heading) * 9.0f, car.y - std::sin(car.heading) * 9.0f, 3.6f};
        const amr::Mat4 proj = amr::perspective(1.05f, static_cast<float>(kInternalW) / kInternalH, 0.5f, 400.0f);
        const amr::Mat4 view = amr::look_at(eye, {car.x, car.y, 1.2f});
        glUseProgram(prog);
        glUniform3f(u_eye, eye.x, eye.y, eye.z);
        glUniform3f(u_fog, sky.x, sky.y, sky.z);
        glBindVertexArray(box.vao);
        auto draw = [&](const amr::Mat4& model, amr::Vec3 col, int checker) {
            const amr::Mat4 mvp = proj * view * model;
            glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(u_model, 1, GL_FALSE, model.m);
            glUniform3f(u_color, col.x, col.y, col.z);
            glUniform1i(u_checker, checker);
            glDrawElements(GL_TRIANGLES, box.count, GL_UNSIGNED_SHORT, nullptr);
        };
        amr::Mat4 ground;                                    // a flat 1 km yard (a very wide, thin box)
        ground.at(0, 0) = 1000; ground.at(1, 1) = 1000; ground.at(2, 2) = 0.1f; ground.at(2, 3) = -0.1f;
        draw(ground, {0.33f, 0.62f, 0.30f}, 1);
        for (int i = -5; i <= 5; ++i)                       // a ring of red-brick blocks to drive between
            for (int j = -5; j <= 5; ++j) {
                if ((i + j) % 3 != 0 || (i == 0 && j == 0)) continue;
                amr::Mat4 m = amr::translate({i * 40.0f, j * 40.0f, 0});
                m.at(0, 0) = 14; m.at(1, 1) = 10; m.at(2, 2) = 6.0f + static_cast<float>((i * 7 + j * 3) & 7);
                draw(m, {0.70f, 0.33f, 0.27f}, 0);
            }
        amr::Mat4 body = amr::translate({car.x, car.y, 0.3f}) * amr::rotate_z(car.heading);
        body.at(0, 0) *= 4.2f; body.at(1, 0) *= 4.2f;        // 4.2 x 1.9 x 1.3 m: the yellow SUV's size
        body.at(0, 1) *= 1.9f; body.at(1, 1) *= 1.9f;
        body.at(2, 2) = 1.3f;
        draw(body, {0.96f, 0.86f, 0.24f}, 0);

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(win, &ww, &wh);
        target.present(ww, wh);
        SDL_GL_SwapWindow(win);
        ++frame;
        if (args.frames >= 0 && frame >= args.frames) running = false;
    }
    int code = 0;
    if (!args.screenshot.empty()) {
        if (target.save_bmp(args.screenshot)) {
            std::printf("wrote %s (car at %.1f, %.1f)\n", args.screenshot.c_str(), car.x, car.y);
        } else {
            std::fprintf(stderr, "could not save %s: %s\n", args.screenshot.c_str(), SDL_GetError());
            code = 1;
        }
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
