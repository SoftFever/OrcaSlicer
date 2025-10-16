{
  description = "";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {
        inherit system;
      };
    in {
      devShells.default = pkgs.mkShellNoCC {
        buildInputs = with pkgs; [
          # build tools
          cmake
          gettext
          libtool
          automake
          autoconf
          texinfo

          # build dependencies
          zlib
          libpng
          expat
          curl
          libjpeg
          freetype
          openssl_3

          opencv
        ];
        shellHook = ''
          export CMAKE_ARGS="-DUSE_SYSTEM_ZLIB=ON"
          export ZLIB_ROOT="${pkgs.zlib}"
        '';
        # patch = ''
        #   --- a/deps/CMakeLists.txt
        #   +++ b/deps/CMakeLists.txt
        #   @@ -348,7 +348,6 @@ if((REV_PARSE_RESULT EQUAL 0) AND (REV_PARSE_OUTPUT STREQUAL "true"))
        #    endif ()

        #    include(OCCT/OCCT.cmake)
        #   -include(OpenCV/OpenCV.cmake)

        #    set(_dep_list
        #        dep_Boost
        #   @@ -360,7 +359,6 @@ set(_dep_list
        #        dep_NLopt
        #        dep_OpenVDB
        #        dep_OpenCSG
        #   -    dep_OpenCV
        #        dep_CGAL
        #        dep_GLFW
        #        dep_OCCT
        #   ''
      };
    });
}
