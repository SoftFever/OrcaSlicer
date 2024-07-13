ExternalProject_Add(dep_OpenMPI
    URL "https://download.open-mpi.org/release/open-mpi/v5.0/openmpi-5.0.3.tar.bz2"
    URL_HASH SHA256=990582f206b3ab32e938aa31bbf07c639368e4405dca196fabe7f0f76eeda90b
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/OpenMPI
    BUILD_IN_SOURCE ON 
    CONFIGURE_COMMAND ./configure ${_cross_compile_arg} --disable-dlopen --enable-cxx=yes "--prefix=${DESTDIR}" 
    BUILD_COMMAND     make -j
    INSTALL_COMMAND   make install
)
