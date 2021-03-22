prusaslicer_add_cmake_project(Cereal
    URL "https://github.com/USCiLab/cereal/archive/v1.2.2.tar.gz"
#    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_ARGS
        -DJUST_INSTALL_CEREAL=on
)