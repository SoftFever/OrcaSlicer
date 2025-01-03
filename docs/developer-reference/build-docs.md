# Build Orca-Slicer Mkdocs 

!!! note 
    It requie docker or podman to build docs.

It will generate a `site` folder below `~/OrcaSlicer`.

``` shell
git clone https://github.com/SoftFever/OrcaSlicer.git
cd ~/OrcaSlicer
docker run --rm -it -v ${PWD}:/docs squidfunk/mkdocs-material build
```
