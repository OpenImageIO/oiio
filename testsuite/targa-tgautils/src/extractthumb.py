#!/usr/bin/env python

from __future__ import print_function
from __future__ import absolute_import
import OpenImageIO as oiio
import sys

if len(sys.argv) < 3 :
    print("No filenames specified")
    exit(1)

inname = sys.argv[1]
outname = sys.argv[2]
buf = oiio.ImageBuf(inname)
if buf.has_thumbnail :
    th = buf.thumbnail()
    th.write(outname, dtype='uint8')
