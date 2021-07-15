#!/usr/bin/env python

import OpenImageIO as oiio

imagedir = OIIO_TESTSUITE_IMAGEDIR + "/targa"

files = [ "CBW8.TGA", "CCM8.TGA", "CTC16.TGA", "CTC24.TGA", "CTC32.TGA",
          "UBW8.TGA", "UCM8.TGA", "UTC16.TGA", "UTC24.TGA", "UTC32.TGA",
          "round_grill.tga" ]
for f in files:
    command += rw_command (imagedir, f)


# Test ability to extract thumbnails
outputs = [ ]
for f in files:
    fin = imagedir + "/" + f
    buf = oiio.ImageBuf(fin)
    if buf.has_thumbnail :
        thumbname = f + ".tif"
        command += run_app(pythonbin + " src/extractthumb.py " + fin + " " + thumbname)
        outputs += [ thumbname ]
outputs += [ 'out.txt' ]
