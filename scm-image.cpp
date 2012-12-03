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

#include "util3d/glsl.h"

#include "scm-image.hpp"
#include "scm-index.hpp"

//------------------------------------------------------------------------------

scm_image::scm_image(const std::string& name,
					 const std::string& scm, scm_cache *cache, int c, bool h) :
	name(name),
	cache(cache),
	chan(c),
    height(h),
    k0(0),
    k1(1)
{
	file = cache->add_file(scm);
}

//------------------------------------------------------------------------------

void scm_image::bind(GLint unit, GLuint program) const
{
    const int s = cache->get_grid_size();
    const int n = cache->get_page_size();

    GLuint uS  = glsl_uniform(program, "%s.S",  name.c_str());
    GLuint ur  = glsl_uniform(program, "%s.r",  name.c_str());
    GLuint uk0 = glsl_uniform(program, "%s.k0", name.c_str());
    GLuint uk1 = glsl_uniform(program, "%s.k1", name.c_str());

    glUniform1f(uk0, k0);
    glUniform1f(uk1, k1);
    glUniform1i(uS, unit);
    glUniform2f(ur, GLfloat(n) / (n + 2) / s,
                    GLfloat(n) / (n + 2) / s);

    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, cache->get_texture());
}

void scm_image::unbind(GLint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

//------------------------------------------------------------------------------

void scm_image::bind_page(GLuint program, int d, int t, long long i) const
{
    GLint ua = glsl_uniform(program, "%s.a[%d]", name.c_str(), d);
    GLint ub = glsl_uniform(program, "%s.b[%d]", name.c_str(), d);

    int u;
    int l = cache->get_page(file, i, t, u);

    double a = (t - u) / 60.0;

    if      (l ==  0) a = 0.0;
    else if (a > 1.0) a = 1.0;
    else if (a < 0.0) a = 0.0;

    const int s = cache->get_grid_size();
    const int n = cache->get_page_size();

    glUniform1f(ua, GLfloat(a));
    glUniform2f(ub, GLfloat((l % s) * (n + 2) + 1) / GLfloat(s * (n + 2)),
                    GLfloat((l / s) * (n + 2) + 1) / GLfloat(s * (n + 2)));
}

void scm_image::unbind_page(GLuint program, int d) const
{
    GLint ua = glsl_uniform(program, "%s.a[%d]", name.c_str(), d);
    GLint ub = glsl_uniform(program, "%s.b[%d]", name.c_str(), d);

    glUniform1f(ua, 0.f);
    glUniform2f(ub, 0.f, 0.f);
}

void scm_image::touch_page(long long i, int t) const
{
    int u;
    cache->get_page(file, i, t, u);
}

//------------------------------------------------------------------------------

float scm_image::get_page_sample(const double *v) const
{
    return cache->get_page_sample(file, v) * (k1 - k0) + k0;
}

void scm_image::get_page_bounds(long long i, float& r0, float& r1) const
{
    cache->get_page_bounds(file, i, r0, r1);

    r0 = r0 * (k1 - k0) + k0;
    r1 = r1 * (k1 - k1) + k1;
}

bool scm_image::get_page_status(long long i) const
{
    return cache->get_page_status(file, i);
}

//------------------------------------------------------------------------------
