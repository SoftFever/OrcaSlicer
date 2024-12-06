# Also include the core source directory
include ../src/Sources

# Various binaries we're using
DOXYGEN=`which doxygen`

# Source code files

html: Doxyfile $(HEADERS)
	$(DOXYGEN) Doxyfile

.PHONY: clean
clean:
	-rm -rf html
