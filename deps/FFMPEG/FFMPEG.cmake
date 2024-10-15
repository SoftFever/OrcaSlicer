set(_conf_cmd ./configure)

if (MSVC)
    set(_dstdir ${DESTDIR}/usr/local)
    set(_source_dir "${CMAKE_BINARY_DIR}/dep_FFMPEG-prefix/src/dep_FFMPEG")
    ExternalProject_Add(dep_FFMPEG
        URL https://github.com/bambulab/ffmpeg_prebuilts/releases/download/7.0.2/7.0.2_msvc.zip
        URL_HASH SHA256=DF44AE6B97CE84C720695AE7F151B4A9654915D1841C68F10D62A1189E0E7181
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFMPEG
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND
            # COMMAND ${CMAKE_COMMAND} -E make_directory "${_dstdir}/bin"
            # COMMAND ${CMAKE_COMMAND} -E make_directory "${_dstdir}/lib"
            # COMMAND ${CMAKE_COMMAND} -E make_directory "${_dstdir}/include"
            COMMAND ${CMAKE_COMMAND} -E copy_directory  "${_source_dir}/bin" "${_dstdir}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_directory  "${_source_dir}/lib" "${_dstdir}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_directory  "${_source_dir}/include" "${_dstdir}/include"
    )

else ()
    set(_extra_cmd "--pkg-config-flags=\"--static\"")
    string(APPEND _extra_cmd "--extra-cflags=\"-I ${DESTDIR}/usr/local/include\"")
    string(APPEND _extra_cmd "--extra-ldflags=\"-I ${DESTDIR}/usr/local/lib\"")
    string(APPEND _extra_cmd "--extra-libs=\"-lpthread -lm\"")
    string(APPEND _extra_cmd "--ld=\"g++\"")
    string(APPEND _extra_cmd "--bindir=\"${DESTDIR}/usr/local/bin\"")
    string(APPEND _extra_cmd "--enable-gpl")
    string(APPEND _extra_cmd "--enable-nonfree")


    ExternalProject_Add(dep_FFMPEG
        URL https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.0.2.tar.gz
        URL_HASH SHA256=5EB46D18D664A0CCADF7B0ADEE03BD3B7FA72893D667F36C69E202A807E6D533
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFMPEG
        CONFIGURE_COMMAND ${_conf_cmd}
            "--prefix=${DESTDIR}/usr/local"
            "--enable-shared"
            "--disable-doc"
            "--enable-small"
            "--disable-outdevs"
            "--disable-filters"
            "--enable-filter=*null*,afade,*fifo,*format,*resample,aeval,allrgb,allyuv,atempo,pan,*bars,color,*key,crop,draw*,eq*,framerate,*_qsv,*_vaapi,*v4l2*,hw*,scale,volume,test*"
            "--disable-protocols"
            "--enable-protocol=file,fd,pipe,rtp,udp"
            "--disable-muxers"
            "--enable-muxer=rtp"
            "--disable-encoders"
            "--disable-decoders"
            "--enable-decoder=*aac*,h264*,mp3*,mjpeg,rv*"
            "--disable-demuxers"
            "--enable-demuxer=h264,mp3,mov"
            "--disable-zlib"
            "--disable-avdevice"
        BUILD_IN_SOURCE ON
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
    )

endif()