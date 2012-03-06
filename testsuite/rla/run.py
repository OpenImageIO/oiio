#!/usr/bin/python 

import sys
sys.path = ["..", "testsuite"] + sys.path
import runtest

imagedir = runtest.parent + "/oiio-images"
files = [ "ginsu_a_nc10.rla", "ginsu_a_ncf.rla", "ginsu_rgba_nc8.rla", \
          "ginsu_rgb_nc16.rla", "imgmake_rgba_nc10.rla", "ginsu_a_nc16.rla", \
          "ginsu_rgba_nc10.rla", "ginsu_rgba_ncf.rla", "ginsu_rgb_nc8.rla", \
          "imgmake_rgba_nc16.rla", "ginsu_a_nc8.rla", "ginsu_rgba_nc16.rla", \
          "ginsu_rgb_nc10.rla", "ginsu_rgb_ncf.rla", "imgmake_rgba_nc8.rla" ]
command = ""
for f in files:
    command = command + runtest.rw_command (imagedir, f)

# boilerplate
ret = runtest.runtest (command)
sys.exit (ret)
