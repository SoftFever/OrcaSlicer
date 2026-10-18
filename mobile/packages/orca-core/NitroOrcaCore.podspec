require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

# The slicing core arrives as a prebuilt static XCFramework, produced by
# scripts/build_ios_core.sh (or the "Build iOS core" workflow) and placed at
# ios/OrcaCore.xcframework. Its headers are the façade's headers only; the Hybrid
# Objects in cpp/ wrap them.
Pod::Spec.new do |s|
  s.name         = "NitroOrcaCore"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = "https://github.com/angelo-hub/orcaslicer"
  s.license      = package["license"]
  s.authors      = "OrcaSlicer contributors"

  s.platforms    = { :ios => 16.0 }
  s.source       = { :git => "https://github.com/angelo-hub/orcaslicer.git", :tag => "#{s.version}" }

  s.source_files = [
    # Hybrid Objects (C++)
    "cpp/**/*.{hpp,cpp}",
    # The viewport: Nitro View (Swift) over a Metal renderer (Objective-C++)
    "ios/**/*.{h,mm,swift}",
  ]
  # The renderer's Objective-C interface must be visible to the Swift view.
  s.public_header_files = ["ios/OrcaViewportRenderer.h"]

  s.vendored_frameworks = "ios/OrcaCore.xcframework"
  # The core is a static archive of C++ code; iconv, libz, liblzma and libc++
  # come from the iOS system so the archive can stay lean. liblzma satisfies
  # openexr's deep-image compressor and CoreGraphics resolves the CG* imports
  # that OpenCV's ImageIO backend and Model I/O reach for on Apple. libpng is
  # bundled inside OrcaCore because iOS ships none.
  s.libraries = "c++", "iconv", "z", "lzma"
  s.frameworks = "Foundation", "ModelIO", "Metal", "MetalKit", "QuartzCore", "CoreGraphics", "WebKit", "UIKit"

  s.pod_target_xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    "GCC_PREPROCESSOR_DEFINITIONS" => "$(inherited) BOOST_ALL_NO_LIB",
    "DEFINES_MODULE" => "YES",
    "SWIFT_OBJC_INTEROP_MODE" => "objcxx",
  }

  load 'nitrogen/generated/ios/NitroOrcaCore+autolinking.rb'
  add_nitrogen_files(s)

  s.dependency 'React-jsi'
  s.dependency 'React-callinvoker'
  install_modules_dependencies(s)
end
