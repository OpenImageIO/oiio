/*
  Copyright 2010 Larry Gritz and the other authors and contributors.
  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the software's owners nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  (This is the Modified BSD License)
*/


/////////////////////////////////////////////////////////////////////////////
// Private definitions internal to the socket.imageio plugin
/////////////////////////////////////////////////////////////////////////////



#include <map>

#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>

#include "socket_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace boost;
using namespace boost::asio;

namespace socket_pvt {

std::size_t
socket_write (ip::tcp::socket &s, TypeDesc &type, const void *data, int size)
{
    std::size_t bytes;

    // TODO: Translate data to correct endianesss.
    bytes = write (s, buffer (reinterpret_cast<const char *> (data), size));

    return bytes;
}

// utility for calcuating the bytes of a cropped edge tile
int
tile_bytes_at(const ImageSpec &spec, int x, int y, int z)
{
    if (spec.tile_width <= 0 || spec.tile_height <= 0 || spec.tile_depth <= 0)
        return 0;

    int tile_width = std::min(spec.full_width, (x + spec.tile_width)) - x;
    int tile_height = std::min(spec.full_height, (y + spec.tile_height)) - y;

    imagesize_t r = clamped_mult64 ((imagesize_t)tile_width,
                                    (imagesize_t)tile_height);
    if (spec.tile_depth > 1) {
        int tile_depth = std::min(spec.full_depth, (z + spec.tile_depth)) - z;
        r = clamped_mult64 (r, (imagesize_t)tile_depth);
    }

    return r * spec.nchannels * spec.format.size();
}

}

OIIO_PLUGIN_NAMESPACE_END

