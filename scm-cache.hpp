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

#ifndef SCM_CACHE_HPP
#define SCM_CACHE_HPP

#include <vector>
#include <string>

#include <GL/glew.h>

#include "scm-queue.hpp"
#include "scm-fifo.hpp"
#include "scm-task.hpp"
#include "scm-set.hpp"

//------------------------------------------------------------------------------

class scm_system;

//------------------------------------------------------------------------------

/// An scm_cache is a virtual texture, demand-paged with threaded data access,
/// represented as a single large OpenGL texture atlas.

class scm_cache
{
public:

    static int cache_size;
    static int cache_threads;
    static int need_queue_size;
    static int load_queue_size;
    static int loads_per_cycle;

    scm_cache(scm_system *, int, int, int);
   ~scm_cache();

    void   add_load(scm_task&);

    int    get_grid_size() const { return s; }
    int    get_page_size() const { return n; }

    GLuint get_texture() const;
    int    get_page(int, long long, int, int&);

    void   update(int, bool);
    void   render(int, int);
    void   flush ();

private:

    scm_system         *sys;
    scm_set             pages;  // Page set currently active
    scm_set             waits;  // Page set currently being loaded
    scm_queue<scm_task> loads;  // Page loader queue
    scm_fifo <GLuint>   pbos;   // Asynchronous upload ring

    GLuint texture;             // Atlas texture object
    int    s;                   // Atlas width and height in pages
    int    l;                   // Atlas current page
    int    n;                   // Page width and height in pixels
    int    c;                   // Channels per pixel
    int    b;                   // Bits per channel

    int get_slot(int, long long);
};

typedef std::vector<scm_cache *>           scm_cache_v;
typedef std::vector<scm_cache *>::iterator scm_cache_i;

//------------------------------------------------------------------------------

#endif
