.PHONY: all here clean
all: here model module

here:
	cp ../src/*.h .
model:
	(mkdir $@ && cp ../src/model/*.h $@)
module:
	(mkdir $@ && cp ../src/$@/*.h $@)

clean:
	-rm *.h
	-rm -rf model
	-rm -rf module
