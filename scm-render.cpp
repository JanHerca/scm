// Copyright (C) 2011-2012 Robert Kooima
//
// LIBSCM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITH-
// OUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.

#include <cstdlib>
#include <cmath>
#include <cstring>

#include "util3d/math3d.h"

#include "scm-render.hpp"
#include "scm-sphere.hpp"
#include "scm-scene.hpp"
#include "scm-state.hpp"
#include "scm-frame.hpp"
#include "scm-log.hpp"

//------------------------------------------------------------------------------

static void fillscreen(int, int);

static void wire_on();
static void wire_off();

//------------------------------------------------------------------------------

/// Create a new render manager. Initialize the necessary OpenGL state
/// framebuffer object state.
///
/// Motion blur is disabled (set to zero) by default.
///
/// @param w Width of the off-screen render targets (in pixels).
/// @param h Height of the off-screen render targets (in pixels).

scm_render::scm_render(int w, int h) :
    width(w), height(h), blur(0), wire(false), frame0(0), frame1(0)
{
    init_ogl();
    init_matrices();

    for (int i = 0; i < 16; i++)
        midentity(previous_T[i]);
}

/// Finalize all OpenGL state.

scm_render::~scm_render()
{
    free_ogl();
}

//------------------------------------------------------------------------------

/// Set the size of the off-screen render targets. This entails the destruction
/// and recreation of OpenGL framebuffer objects, so it should *not* be called
/// every frame.

void scm_render::set_size(int w, int h)
{
    free_ogl();
    width  = w;
    height = h;
    init_ogl();
    init_matrices();
}

/// Set the motion blur degree. Higher degrees incur greater rendering loads.
/// 8 is an effective value. Set 0 to disable motion blur completely.

void scm_render::set_blur(int b)
{
    blur = b;
}

/// Set the wireframe option.

void scm_render::set_wire(bool w)
{
    wire = w;
}

//------------------------------------------------------------------------------

/// Render the foreground and background with optional blur and dissolve.
///
/// @param sphere  Sphere geometry manager to perform the rendering
/// @param state   Viewer and environment state
/// @param P       Projection matrix in OpenGL column-major order
/// @param M       Model-view matrix in OpenGL column-major order
/// @param channel Channel index
/// @param frame   Frame number

void scm_render::render(scm_sphere *sphere,
                  const scm_state  *state,
                  const double     *P,
                  const double     *M, int channel, int frame)
{
    scm_scene *foreground0 = state->get_foreground0();
    scm_scene *foreground1 = state->get_foreground1();
    scm_scene *background0 = state->get_background0();
    scm_scene *background1 = state->get_background1();

    const double t = state->get_fade();

    GLfloat blur_T[16];

    const bool do_blur = check_blur(P, M, blur_T, previous_T[channel]);
    const bool do_fade = check_fade(foreground0, foreground1,
                                    background0, background1, t);

    if (!do_fade && !do_blur)
        render(sphere, foreground0, background0, P, M, channel, frame);

    else
    {
        GLint framebuffer;

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

        // Render the scene(s) to the offscreen framebuffers.

        glPushAttrib(GL_VIEWPORT_BIT | GL_SCISSOR_BIT);
        {
            frame0->bind_frame();
            render(sphere, foreground0, background0, P, M, channel, frame);

            if (do_fade)
            {
                frame1->bind_frame();
                render(sphere, foreground1, background1, P, M, channel, frame);
            }
        }
        glPopAttrib();

        // Bind the resulting textures.

        glActiveTexture(GL_TEXTURE3);
        frame1->bind_depth();
        glActiveTexture(GL_TEXTURE2);
        frame0->bind_depth();
        glActiveTexture(GL_TEXTURE1);
        frame1->bind_color();
        glActiveTexture(GL_TEXTURE0);
        frame0->bind_color();
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        // Bind the necessary shader and set its uniforms.

        if      (do_fade && do_blur)
        {
            glUseProgram(render_both.program);
            glUniform1f       (uniform_both_t,       GLfloat(t));
            glUniform1i       (uniform_both_n,       blur);
            glUniformMatrix4fv(uniform_both_T, 1, 0, blur_T);
        }
        else if (do_fade && !do_blur)
        {
            glUseProgram(render_fade.program);
            glUniform1f       (uniform_fade_t,       GLfloat(t));
        }
        else if (!do_fade && do_blur)
        {
            glUseProgram(render_blur.program);
            glUniform1i       (uniform_blur_n,       blur);
            glUniformMatrix4fv(uniform_blur_T, 1, 0, blur_T);
        }

        // Render the blur / fade to the framebuffer.

        fillscreen(width, height);
        glUseProgram(0);
    }
}

/// Render the background and foreground spheres, with atmosphere if configured,
/// but without blur or dissolve.
///
/// This function is usually called by the previous function as needed to
/// produce the desired effects. Calling it directly is a legitimate means
/// of circumventing these options.
///
/// @param sphere     Sphere geometry manager to perform the rendering
/// @param foreground Foreground scene
/// @param background Background scene
/// @param P          Projection matrix in OpenGL column-major order
/// @param M          Model-view matrix in OpenGL column-major order
/// @param channel    Channel index
/// @param frame      Frame number

void scm_render::render(scm_sphere *sphere,
                        scm_scene  *foreground,
                        scm_scene  *background,
                      const double *P,
                      const double *M, int channel, int frame)
{
    double T[16];

    // If there is a foreground sphere, get its atmospheric parameters.

    scm_atmo atmo;

    if (foreground && !wire)
        atmo = foreground->get_atmo();

    // If there is an atmosphere, bind the temporary render target.

    GLint framebuffer;

    if (atmo.H > 0)
    {
        glPushAttrib(GL_VIEWPORT_BIT | GL_SCISSOR_BIT);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        frameA->bind_frame();
    }

    // If we're going to be doing rendering, clear the buffers.

    if (background || foreground)
    {
        GLuint c = background ? background->get_clear() :
                   foreground ? foreground->get_clear() : 0;

        glClearColor(GLfloat((c & 0xFF000000) >> 24) / 255.0,
                     GLfloat((c & 0x00FF0000) >> 16) / 255.0,
                     GLfloat((c & 0x0000FF00) >>  8) / 255.0,
                     GLfloat((c & 0x000000FF) >>  0) / 255.0);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // Render the background

    if (background)
    {
        // Extract only the rotation of the view matrix.

        double N[16], Q[16], T[16], I[16];

        midentity(N);
        vnormalize(N + 0, M + 0);
        vnormalize(N + 4, M + 4);
        vnormalize(N + 8, M + 8);

        // Remove any offset in the projection matrix.

        double w[4], v[4] = { 0.0, 0.0, -1.0, 0.0 };

        minvert(I, P);
        wtransform(w, I, v);
        w[0] /= w[3];
        w[1] /= w[3];
        w[2] /= w[3];
        mtranslate(T, w);
        mmultiply(Q, P, T);

        // Apply the transform.

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixd(Q);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(N);

        mmultiply(T, Q, N);

        // Render the inside of the sphere.

        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        {
            glEnable(GL_DEPTH_CLAMP);
            glDisable(GL_DEPTH_TEST);
            glFrontFace(GL_CCW);

            if (wire) wire_on();
            sphere->draw(background, T, width, height, channel, frame);
            if (wire) wire_off();

            background->draw_label();
        }
        glPopAttrib();

        // Clear the alpha channel to distinguish background from foreground.

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColorMask(GL_TRUE,  GL_TRUE,  GL_TRUE,  GL_TRUE);
    }

    // Render the foreground

    if (foreground)
    {
        // Apply the transform.

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixd(P);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(M);

        mmultiply(T, P, M);

        // Render the outside of the sphere.

        glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT);
        {
            glFrontFace(GL_CW);

            if (wire) wire_on();
            sphere->draw(foreground, T, width, height, channel, frame);
            if (wire) wire_off();

            glEnable(GL_CLIP_PLANE0);
            foreground->draw_label();
        }
        glPopAttrib();
    }

    // Render the atmosphere

    if (atmo.H > 0)
    {
        glPopAttrib();

        // Bind the color and depth buffers of the temporary render target.

        glActiveTexture(GL_TEXTURE2);
        frameA->bind_depth();
        glActiveTexture(GL_TEXTURE0);
        frameA->bind_color();
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        // Prepare the atmosphere shader.

        GLfloat atmo_r[ 2];
        GLfloat atmo_p[ 4];
        GLfloat atmo_T[16];

        check_atmo(P, M, atmo_T, atmo_p);

        atmo_r[0] = foreground->get_minimum_ground();
        atmo_r[1] = atmo_r[0] - atmo.H * logf(0.00001f);

        glUseProgram(render_atmo.program);
        glUniform1f       (uniform_atmo_P,       atmo.P);
        glUniform1f       (uniform_atmo_H,       atmo.H);
        glUniform3fv      (uniform_atmo_c, 1,    atmo.c);
        glUniform2fv      (uniform_atmo_r, 1,    atmo_r);
        glUniform3fv      (uniform_atmo_p, 1,    atmo_p);
        glUniformMatrix4fv(uniform_atmo_T, 1, 0, atmo_T);

        // Render the atmosphere to the framebuffer.

        fillscreen(width, height);
        glUseProgram(0);
    }
}

//------------------------------------------------------------------------------

/// Initialize the uniforms of the given GLSL program object.

void scm_render::init_uniforms(GLuint program)
{
    glUseProgram(program);
    {
        glUniform1i(glGetUniformLocation(program, "color0"), 0);
        glUniform1i(glGetUniformLocation(program, "color1"), 1);
        glUniform1i(glGetUniformLocation(program, "depth0"), 2);
        glUniform1i(glGetUniformLocation(program, "depth1"), 3);
    }
    glUseProgram(0);
}

void scm_render::init_matrices()
{
    double w = double(width);
    double h = double(height);

    // A transforms a fragment coordinate to a texture coordinate.

    A[0] = 1/w; A[4] = 0.0; A[ 8] = 0.0; A[12] =  0.0;
    A[1] = 0.0; A[5] = 1/h; A[ 9] = 0.0; A[13] =  0.0;
    A[2] = 0.0; A[6] = 0.0; A[10] = 1.0; A[14] =  0.0;
    A[3] = 0.0; A[7] = 0.0; A[11] = 0.0; A[15] =  1.0;

    // B transforms a texture coordinate to a normalized device coordinace.

    B[0] = 2.0; B[4] = 0.0; B[ 8] = 0.0; B[12] = -1.0;
    B[1] = 0.0; B[5] = 2.0; B[ 9] = 0.0; B[13] = -1.0;
    B[2] = 0.0; B[6] = 0.0; B[10] = 2.0; B[14] = -1.0;
    B[3] = 0.0; B[7] = 0.0; B[11] = 0.0; B[15] =  1.0;

    // C transforms a normalized device coordinate to a texture coordinate

    C[0] = 0.5; C[4] = 0.0; C[ 8] = 0.0; C[12] =  0.5;
    C[1] = 0.0; C[5] = 0.5; C[ 9] = 0.0; C[13] =  0.5;
    C[2] = 0.0; C[6] = 0.0; C[10] = 0.5; C[14] =  0.5;
    C[3] = 0.0; C[7] = 0.0; C[11] = 0.0; C[15] =  1.0;

    // D transforms a texture coordinate to a fragment coordinate.

    D[0] =   w; D[4] = 0.0; D[ 8] = 0.0; D[12] =  0.0;
    D[1] = 0.0; D[5] =   h; D[ 9] = 0.0; D[13] =  0.0;
    D[2] = 0.0; D[6] = 0.0; D[10] = 1.0; D[14] =  0.0;
    D[3] = 0.0; D[7] = 0.0; D[11] = 0.0; D[15] =  1.0;
}

//------------------------------------------------------------------------------

#include "scm_render_fade_vert.h"
#include "scm_render_fade_frag.h"
#include "scm_render_blur_vert.h"
#include "scm_render_blur_frag.h"
#include "scm_render_both_vert.h"
#include "scm_render_both_frag.h"
#include "scm_render_atmo_vert.h"
#include "scm_render_atmo_frag.h"

void scm_render::init_ogl()
{
    glsl_source(&render_fade, (const char *) scm_render_fade_vert,
                                             scm_render_fade_vert_len,
                              (const char *) scm_render_fade_frag,
                                             scm_render_fade_frag_len);
    glsl_source(&render_blur, (const char *) scm_render_blur_vert,
                                             scm_render_blur_vert_len,
                              (const char *) scm_render_blur_frag,
                                             scm_render_blur_frag_len);
    glsl_source(&render_both, (const char *) scm_render_both_vert,
                                             scm_render_both_vert_len,
                              (const char *) scm_render_both_frag,
                                             scm_render_both_frag_len);
    glsl_source(&render_atmo, (const char *) scm_render_atmo_vert,
                                             scm_render_atmo_vert_len,
                              (const char *) scm_render_atmo_frag,
                                             scm_render_atmo_frag_len);

    init_uniforms(render_fade.program);
    init_uniforms(render_blur.program);
    init_uniforms(render_both.program);
    init_uniforms(render_atmo.program);

    glUseProgram(render_fade.program);
    uniform_fade_t = glsl_uniform(render_fade.program, "t");

    glUseProgram(render_blur.program);
    uniform_blur_n = glsl_uniform(render_blur.program, "n");
    uniform_blur_T = glsl_uniform(render_blur.program, "T");

    glUseProgram(render_both.program);
    uniform_both_t = glsl_uniform(render_both.program, "t");
    uniform_both_n = glsl_uniform(render_both.program, "n");
    uniform_both_T = glsl_uniform(render_both.program, "T");

    glUseProgram(render_atmo.program);
    uniform_atmo_p = glsl_uniform(render_atmo.program, "p");
    uniform_atmo_c = glsl_uniform(render_atmo.program, "atmo_c");
    uniform_atmo_r = glsl_uniform(render_atmo.program, "atmo_r");
    uniform_atmo_T = glsl_uniform(render_atmo.program, "atmo_T");
    uniform_atmo_P = glsl_uniform(render_atmo.program, "atmo_P");
    uniform_atmo_H = glsl_uniform(render_atmo.program, "atmo_H");

    glUseProgram(0);

    frameA = new scm_frame(width, height);
    frame0 = new scm_frame(width, height);
    frame1 = new scm_frame(width, height);

    scm_log("scm_render init_ogl %d %d", width, height);
}

void scm_render::free_ogl()
{
    scm_log("scm_render free_ogl %d %d", width, height);

    delete frameA;
    delete frame0;
    delete frame1;

    frameA = frame0 = frame1 = 0;

    glsl_delete(&render_fade);
    glsl_delete(&render_blur);
    glsl_delete(&render_both);
    glsl_delete(&render_atmo);
}

//------------------------------------------------------------------------------

/// Determine whether fading is necessary.

bool scm_render::check_fade(const scm_scene *foreground0,
                            const scm_scene *foreground1,
                            const scm_scene *background0,
                            const scm_scene *background1, double t)
{
    if (t < 1.0 / 255.0) return false;
    if (foreground0 != foreground1) return true;
    if (background0 != background1) return true;
    return false;
}

/// Determine whether blurring is necessary and compute its transform.

bool scm_render::check_blur(const double *P,
                            const double *M, GLfloat *U, double *S)
{
    if (blur)
    {
        double T[16];
        double I[16];
        double N[16];

        // T is the current view-projection transform. S is the previous one.

        mmultiply(T, P, M);

        if (T[ 0] != S[ 0] || T[ 1] != S[ 1] ||
            T[ 2] != S[ 2] || T[ 3] != S[ 3] ||
            T[ 4] != S[ 4] || T[ 5] != S[ 5] ||
            T[ 6] != S[ 6] || T[ 7] != S[ 7] ||
            T[ 8] != S[ 8] || T[ 9] != S[ 9] ||
            T[10] != S[10] || T[11] != S[11] ||
            T[12] != S[12] || T[13] != S[13] ||
            T[14] != S[14] || T[15] != S[15])
        {
            // Compose a transform taking current fragment coordinates to the
            // fragment coordinates of the previous frame.

            minvert (I, T);    // Inverse of the current view-projection.
            mcpy    (N, D);    // 6. Texture coordinate to fragment coordinate
            mcompose(N, C);    // 5. NDC to texture coordinate
            mcompose(N, S);    // 4. World coordinate to previous NDC
            mcompose(N, I);    // 3. NDC to current world coordinate
            mcompose(N, B);    // 2. Texture coordinate to NDC
            mcompose(N, A);    // 1. Fragment coordinate to texture coordinate
            mcpy    (S, T);    // Store the current transform til next frame

            // Return this matrix for use as an OpenGL uniform.

            for (int i = 0; i < 16; i++)
                U[i] = GLfloat(N[i]);

            return true;
        }
    }
    return false;
}

/// Compute the atmosphere rendering tranform.

void scm_render::check_atmo(const double *P,
                            const double *M, GLfloat *U, GLfloat *p)
{
    double T[16];
    double I[16];
    double N[16];

    // Compose a transform taking fragment coordinates to world coordinates.

    mmultiply(T, P, M);  // Current view-projection transform
    minvert  (N, T);     // 3. NDC to current world coordinate
    mcompose (N, B);     // 2. Texture coordinate to NDC
    mcompose (N, A);     // 1. Fragment coordinate to texture coordinate

    // Return this matrix for use as an OpenGL uniform.

    for (int i = 0; i < 16; i++)
        U[i] = GLfloat(N[i]);

    // Return the view position for use as an OpenGL uniform.

    minvert(I, T);

    p[0] = GLfloat(I[ 8] / I[11]);
    p[1] = GLfloat(I[ 9] / I[11]);
    p[2] = GLfloat(I[10] / I[11]);
}

//------------------------------------------------------------------------------

/// Draw a screen-filling rectangle.

static void fillscreen(int w, int h)
{
    glPushAttrib(GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT);
    {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glFrontFace(GL_CCW);
        glDepthMask(GL_FALSE);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glBegin(GL_QUADS);
        {
            glTexCoord2i(0, 0); glVertex2f(-1.0f, -1.0f);
            glTexCoord2i(w, 0); glVertex2f(+1.0f, -1.0f);
            glTexCoord2i(w, h); glVertex2f(+1.0f, +1.0f);
            glTexCoord2i(0, h); glVertex2f(-1.0f, +1.0f);
        }
        glEnd();

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
    glPopAttrib();
}

/// Set the OpenGL state for wireframe rendering.

static void wire_on()
{
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth(1.0);
}

/// Unset the OpenGL state for wireframe rendering.

static void wire_off()
{
    glPopAttrib();
}

//------------------------------------------------------------------------------
