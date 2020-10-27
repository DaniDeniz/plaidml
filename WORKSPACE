# Bazel Workspace for PlaidML
workspace(name = "com_intel_plaidml")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# define this first in case any repository rules want to use it
http_archive(
    name = "bazel_skylib",
    sha256 = "2ea8a5ed2b448baf4a6855d3ce049c4c452a6470b1efd1504fdb7c1c134d220a",
    strip_prefix = "bazel-skylib-0.8.0",
    url = "https://github.com/bazelbuild/bazel-skylib/archive/0.8.0.tar.gz",
)

git_repository(
    name = "toolchain",
    commit = "5f7d62080568e1bfac7338420509d80a5d6ccffe",
    remote = "https://github.com/DaniDeniz/toolchain",
)

local_repository(
    name = "opengl_repo",
    path = "vendor/opengl",
)

load("//bzl:workspace.bzl", "configure_llvm", "plaidml_workspace")

plaidml_workspace()

configure_llvm()
