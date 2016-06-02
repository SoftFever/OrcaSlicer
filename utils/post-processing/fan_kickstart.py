#!/usr/bin/python
import sys
import re

sea = re.compile("M106 S[1-9]+[0-9]*")
rep = re.compile("M106 S255\n\g<0>")
out = open(sys.argv[1]+"_fixed", 'w')
  with open(sys.argv[1]) as f:
    for r in f:
      if re.search(sea, r) is not None:
        out.write(re.sub(sea,"M106 S255\n\g<0>",r))
      else:
        out.write(r)
