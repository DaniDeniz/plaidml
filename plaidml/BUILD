# Copyright 2018 Intel Corporation.

package(default_visibility = ["//visibility:private"])

load("//bzl:conda.bzl", "conda_binary", "conda_test")
load(
    "//bzl:plaidml.bzl",
    "plaidml_cc_binary",
    "plaidml_cc_library",
    "plaidml_cc_test",
    "plaidml_cc_version",
    "plaidml_macos_dylib",
    "plaidml_proto_library",
    "plaidml_py_library",
    "plaidml_py_wheel",
)
load("@cuda//:build_defs.bzl", "if_cuda_is_configured")
load("@io_bazel_rules_jsonnet//jsonnet:jsonnet.bzl", "jsonnet_to_json")

# The PlaidML configuration protobuf definition.
plaidml_proto_library(
    name = "proto",
    srcs = ["plaidml.proto"],
    visibility = ["//visibility:public"],
)

plaidml_cc_version(
    name = "version",
    prefix = "PLAIDML",
)

PLAIDML_CPP_SOURCES = [
    "config.h",
    "plaidml.cc",
    ":version",
]

PLAIDML_CPP_HEADERS = [
    "plaidml.h",
    "plaidml++.h",
]

PLAIDML_CPP_DEPS = [
    ":proto_cc",
    "//base/config",
    "//base/eventing/file",
    "//base/util:runfiles_db",
    "//plaidml/base",
    "//plaidml/edsl",
    "//tile/base",
    "//tile/base:program_cache",
    "//tile/stripe",
    "//tile/platform/local_machine",
    "//tile/proto:metadata_cc",
    "@boost//:filesystem",
    "@half",
    "@minizip",
] + select({
    "@toolchain//:linux_arm_32v7": [
        "//tile/hal/opencl",
    ],
    "@toolchain//:linux_arm_64v8": [
        "//tile/hal/opencl",
    ],
    "@toolchain//:macos_x86_64": [
        "//tile/hal/cpu",
        "//tile/hal/metal",
        "//tile/hal/opencl",
    ],
    "@toolchain//:windows_x86_64": [
        "//tile/hal/cpu",
        "//tile/hal/opencl",
    ],
    "//conditions:default": [
        "//tile/hal/cpu",
        "//tile/hal/opencl",
    ],
}) + if_cuda_is_configured([
    "//tile/hal/cuda",
])

# The PlaidML C++ library, as used by Bazel targets.
plaidml_cc_library(
    name = "api",
    srcs = PLAIDML_CPP_SOURCES + ["config-bazel.cc"],
    hdrs = PLAIDML_CPP_HEADERS,
    data = [":configs"],
    visibility = ["//visibility:public"],
    deps = PLAIDML_CPP_DEPS,
    alwayslink = 1,
)

plaidml_cc_binary(
    name = "libplaidml.so",
    linkshared = 1,
    tags = ["linux"],
    deps = [":api"],
)

plaidml_cc_binary(
    name = "plaidml.dll",
    linkshared = 1,
    tags = ["msvc"],
    deps = [":api"],
)

apple_binary(
    name = "libplaidml_raw.dylib",
    binary_type = "dylib",
    platform_type = "macos",
    tags = ["darwin"],
    deps = [":api"],
)

plaidml_macos_dylib(
    name = "libplaidml_macos",
    src = ":libplaidml_raw.dylib",
    lib = "libplaidml.dylib",
    tags = ["darwin"],
)

plaidml_cc_library(
    name = "internal",
    srcs = select({
        "@toolchain//:windows_x86_64": [":plaidml.dll"],
        "@toolchain//:macos_x86_64": [":libplaidml_macos"],
        "//conditions:default": [":libplaidml.so"],
    }),
    hdrs = PLAIDML_CPP_HEADERS,
    linkstatic = 1,  # bazel warning suggests setting this
    visibility = ["//visibility:public"],
    deps = [
        "//plaidml/base:api",
        "//plaidml/edsl:public",
        "@half",
    ],
)

plaidml_cc_test(
    name = "plaidml_test",
    srcs = ["plaidml_test.cc"],
    deps = [
        ":api",
        "//testing:matchers",
        "//testing:plaidml_config",
    ],
)

plaidml_cc_test(
    name = "matmul_fuzz_test",
    timeout = "eternal",
    srcs = ["matmul_fuzz_test.cc"],
    deps = [
        ":api",
        "//testing:plaidml_config",
    ],
)

plaidml_cc_test(
    name = "uint8_test",
    srcs = ["uint8_test.cc"],
    deps = [
        ":api",
        "//testing:matchers",
        "//testing:plaidml_config",
    ],
)

plaidml_cc_test(
    name = "cpp_test",
    srcs = ["cpp_test.cc"],
    deps = [
        ":api",
        "//testing:plaidml_config",
    ],
)

plaidml_cc_test(
    name = "network_test",
    size = "large",
    srcs = ["network_test.cc"],
    copts = select({
        "//bzl:with_callgrind": ["-DWITH_CALLGRIND"],
        "//conditions:default": [],
    }),
    data = [
        "testdata/resnet50.tpb",
        "testdata/xception.tpb",
    ],
    tags = ["manual"],
    deps = [
        ":api",
        "//testing:plaidml_config",
    ],
)

# These are the PlaidML configuration files.
genrule(
    name = "configs",
    srcs = select({
        "@toolchain//:macos_x86_64": [
            "configs/macos.json",
            "configs/macos_experimental.json",
        ],
        "//conditions:default": [
            "configs/config.json",
            "configs/experimental.json",
        ],
    }),
    outs = [
        "config.json",
        "experimental.json",
    ],
    cmd = """
    srcs=($(SRCS))
    outs=($(OUTS))
    cp $${srcs[0]} $${outs[0]}
    cp $${srcs[1]} $${outs[1]}
""",
    visibility = ["//visibility:public"],
)

conda_binary(
    name = "enumerate",
    srcs = ["enumerate.py"],
    env = "@com_intel_plaidml_conda//:env",
    deps = [":py"],
)

conda_binary(
    name = "setup",
    srcs = ["plaidml_setup.py"],
    env = "@com_intel_plaidml_conda//:env",
    main = "plaidml_setup.py",
    deps = [":py"],
)

PYTHON_SRCS = [
    "__init__.py",
    "context.py",
    "exceptions.py",
    "library.py",
    "op.py",
    "plaidml_setup.py",
    "settings.py",
    "tile.py",
]

# The PlaidML Python library, as used by Bazel targets.
plaidml_py_library(
    name = "py",
    srcs = PYTHON_SRCS,
    data = [":configs"] + select({
        "@toolchain//:windows_x86_64": [":plaidml.dll"],
        "@toolchain//:macos_x86_64": [":libplaidml.dylib"],
        "//conditions:default": [":libplaidml.so"],
    }),
    visibility = ["//visibility:public"],
)

conda_test(
    name = "settings_test",
    srcs = ["settings_test.py"],
    env = "@com_intel_plaidml_conda//:env",
    deps = [":py"],
)

conda_test(
    name = "py_plaidml_test",
    srcs = ["plaidml_test.py"],
    env = "@com_intel_plaidml_conda//:env",
    main = "plaidml_test.py",
    deps = [
        ":py",
        "//testing:plaidml_py",
    ],
)

conda_test(
    name = "py_tile_test",
    srcs = ["tile_test.py"],
    env = "@com_intel_plaidml_conda//:env",
    main = "tile_test.py",
    deps = [
        ":py",
        "//testing:plaidml_py",
    ],
)

genrule(
    name = "licenses",
    srcs = [
        "@boost//:license",
        "@easylogging//:license",
        "@half//:license",
        "@minizip//:license",
        "@gmock//:license",
        "@jsoncpp//:license",
        "@com_google_protobuf//:license",
        "@six_archive//:license",
    ],
    outs = [
        "boost-LICENSE",
        "easylogging-LICENSE",
        "half-LICENSE",
        "minizip-LICENSE",
        "gmock-LICENSE",
        "jsoncpp-LICENSE",
        "protobuf-LICENSE",
        "six-LICENSE",
    ],
    cmd = "cp $(SRCS) $(@D)/",
)

genrule(
    name = "llvm_license",
    srcs = ["@llvm//:license"],
    outs = ["llvm-LICENSE"],
    cmd = "cp $(SRCS) $(@D)/",
    tags = ["llvm"],
)

# The PlaidML Wheel definition.
plaidml_py_wheel(
    name = "wheel",
    package_name = "plaidml",
    srcs = PYTHON_SRCS + [
        ":licenses",
    ] + select({
        "@toolchain//:windows_x86_64": [],
        "//conditions:default": [":llvm_license"],
    }),
    config = ":not_setup.cfg",
    config_substitutions = select({
        "@toolchain//:windows_x86_64": {
            "{BIN_DATA_FILES}": """Library/bin =
  data/plaidml.dll
  data/pmlc.exe""",
        },
        "@toolchain//:macos_x86_64": {
            "{BIN_DATA_FILES}": """lib =
  data/libplaidml.dylib
bin =
  data/pmlc""",
        },
        "//conditions:default": {
            "{BIN_DATA_FILES}": """lib =
  data/libplaidml.so
bin =
  data/pmlc""",
        },
    }),
    data = PLAIDML_CPP_HEADERS + [
        ":configs",
        "//plaidml/base:base.h",
        "//plaidml/base:base_cpp.h",
        "//plaidml/edsl:edsl.h",
        "//plaidml/edsl:ffi.h",
        "@half//:include/half.hpp",
        "plaidml-config.cmake",
        "//tile/pmlc",
    ] + select({
        "@toolchain//:windows_x86_64": [":plaidml_pub.dll"],
        "@toolchain//:macos_x86_64": [":libplaidml_macos_pub"],
        "//conditions:default": ["libplaidml_pub.so"],
    }),
    data_renames = select({
        "@toolchain//:windows_x86_64": {"plaidml_pub.dll": "plaidml.dll"},
        "@toolchain//:macos_x86_64": {"libplaidml_pub.dylib": "libplaidml.dylib"},
        "//conditions:default": {"libplaidml_pub.so": "libplaidml.so"},
    }),
    package_prefix = "plaidml",
    platform = select({
        "@toolchain//:linux_arm_32v7": "linux_armv7l",
        "@toolchain//:linux_arm_64v8": "linux_aarch64",
        "@toolchain//:macos_x86_64": "macosx_10_10_x86_64",
        "@toolchain//:windows_x86_64": "win_amd64",
        "//conditions:default": "manylinux1_x86_64",
    }),
    python = "py2.py3",
    visibility = ["//visibility:public"],
)

plaidml_cc_library(
    name = "public_lib",
    srcs = PLAIDML_CPP_SOURCES + select({
        "@toolchain//:windows_x86_64": ["config-windows.cc"],
        "//conditions:default": ["config-unix.cc"],
    }),
    hdrs = PLAIDML_CPP_HEADERS,
    deps = PLAIDML_CPP_DEPS,
    alwayslink = 1,
)

plaidml_cc_binary(
    name = "libplaidml_pub.so",
    linkopts = [
        # Prefer internal library definitions.
        "-Wl,-Bsymbolic-functions",

        # Use an explicit linker script.
        "-Wl,$(location :plaidml.lds)",
    ],
    linkshared = 1,
    tags = ["linux"],
    deps = [
        ":plaidml.lds",
        ":public_lib",
    ],
)

plaidml_cc_binary(
    name = "plaidml_pub.dll",
    linkshared = 1,
    tags = ["msvc"],
    win_def_file = ":plaidml.def",
    deps = [":public_lib"],
)

plaidml_cc_binary(
    name = "libplaidml_raw_pub.dylib",
    linkopts = [
        "-dynamiclib",
        "-exported_symbols_list",
        "$(location :plaidml.ld)",
    ],
    tags = ["darwin"],
    deps = [
        ":plaidml.ld",
        ":public_lib",
    ],
)

plaidml_macos_dylib(
    name = "libplaidml_macos_pub",
    src = ":libplaidml_raw_pub.dylib",
    internal_libname = "libplaidml.dylib",
    lib = "libplaidml_pub.dylib",
    tags = ["darwin"],
)

jsonnet_to_json(
    name = "plaidml_link",
    src = "plaidml_link.jsonnet",
    outs = [
        "plaidml.def",
        "plaidml.ld",
        "plaidml.lds",
    ],
    extra_args = ["-S"],
)
