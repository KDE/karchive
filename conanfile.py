from conans import ConanFile, CMake, tools


class KarchiveConan(ConanFile):
    name = "karchive"
    version = "5.61.0"
    license = "GPLv2"
    url = "https://api.kde.org/frameworks/karchive/html/index.html"
    description = "Reading, creating, and manipulating file archives"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    requires = (
        "extra-cmake-modules/[>=5.60.0]@kde/testing", # CMakeLists.txt requires 5.49.0
        "qt/[>=5.11.0]@bincrafters/stable",
        "zlib/1.2.11@conan/stable",
        "bzip2/1.0.6@conan/stable",
        "lzma/5.2.4@bincrafters/stable"
        # TODO: QCH for documentation
    )

    scm = {
        "type": "git",
        "url": "auto",
        "revision": "auto"
    }

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.resdirs = ["share"]
        self.cpp_info.libs = ["KF5Archive"]
        self.cpp_info.includedirs = ['include/KF5', 'include/KF5/KArchive']
