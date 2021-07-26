prusaslicer_add_cmake_project(Cereal
    URL "https://github.com/USCiLab/cereal/archive/v1.2.2.tar.gz"
    URL_HASH SHA256=1921f26d2e1daf9132da3c432e2fd02093ecaedf846e65d7679ddf868c7289c4
    CMAKE_ARGS
        -DJUST_INSTALL_CEREAL=on
)