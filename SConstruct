#!/usr/bin/env python
# Redux Eco Sim build.
#
#   scons                 -> GDExtension (project/bin/windows/redux_eco_sim.*.dll), via godot-cpp
#   scons cli=yes         -> bin/ecosim_cli.exe   (pure C++ core, no godot-cpp; the headless AI tool)
#   scons tests=yes       -> bin/ecosim_tests.exe (unit + script golden tests)
#   scons cli=yes tests=yes  -> both executables
#
# The core (src/pdx, src/builtins, src/world, src/loader, src/sim) never includes godot-cpp, so the
# executables build in a plain environment. Objects go to build/<variant>/ to keep src/ clean.
import os
import sys

from methods import print_error

libname = "redux_eco_sim"
projectdir = "project"

CORE_DIRS = ["pdx", "builtins", "world", "loader", "sim"]

localEnv = Environment(tools=["default"], PLATFORM="")

customs = [os.path.abspath("custom.py")]
opts = Variables(customs, ARGUMENTS)
opts.Add(BoolVariable("cli", "Build the headless ecosim_cli executable", False))
opts.Add(BoolVariable("tests", "Build the ecosim_tests executable", False))
opts.Add(BoolVariable("native_debug", "Build the executables without optimization, with debug info", False))
opts.Update(localEnv)
Help(opts.GenerateHelpText(localEnv))


def core_sources(env, variant):
    env.VariantDir(variant, "src", duplicate=0)
    sources = []
    for d in CORE_DIRS:
        sources += env.Glob("{}/{}/*.cpp".format(variant, d))
    return sources


if localEnv["cli"] or localEnv["tests"]:
    # ---- Native executables (no godot-cpp) ----
    # A fresh, platform-detected environment (localEnv uses PLATFORM="" for godot-cpp's own tool setup),
    # so on Windows this picks MSVC.
    env = Environment(tools=["default"])
    opts.Update(env)
    env.Append(CPPPATH=["#src"])
    if env["CC"] == "cl":
        env.Append(CCFLAGS=["/std:c++17", "/EHsc", "/utf-8", "/permissive-", "/W3", "/MT", "/bigobj"])
        env.Append(CPPDEFINES=["_CRT_SECURE_NO_WARNINGS", "NOMINMAX"])
        if env["native_debug"]:
            env.Append(CCFLAGS=["/Od", "/Zi", "/FS"], LINKFLAGS=["/DEBUG"])
        else:
            env.Append(CCFLAGS=["/O2", "/DNDEBUG"])
    else:
        env.Append(CCFLAGS=["-std=c++17", "-Wall", "-Wextra"])
        env.Append(CCFLAGS=["-O0", "-g"] if env["native_debug"] else ["-O2", "-DNDEBUG"])

    core = core_sources(env, "build/native/src")
    core_lib = env.StaticLibrary("build/native/ecosim_core", core)

    targets = []
    if env["cli"]:
        env.VariantDir("build/native/cli", "src/cli", duplicate=0)
        targets.append(env.Program("bin/ecosim_cli", env.Glob("build/native/cli/*.cpp") + [core_lib]))
    if env["tests"]:
        env.VariantDir("build/native/tests", "tests/unit", duplicate=0)
        targets.append(env.Program("bin/ecosim_tests", env.Glob("build/native/tests/*.cpp") + [core_lib]))
    Default(*targets)
else:
    # ---- GDExtension ----
    if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
        print_error("godot-cpp is missing. Copy it from Rota Fortunae or clone godot-cpp (branch 4.5) into ./godot-cpp")
        sys.exit(1)

    env = localEnv.Clone()
    env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})
    env.Append(CPPPATH=["#src"])
    if env.get("is_msvc", False):
        env.Append(CCFLAGS=["/utf-8", "/bigobj"])
        env.Append(CPPDEFINES=["NOMINMAX"])

    sources = core_sources(env, "build/gdext/src")
    sources += env.Glob("build/gdext/src/godot/*.cpp")
    sources += env.Glob("build/gdext/src/*.cpp")

    suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
    lib_filename = "{}{}{}{}".format(env.subst("$SHLIBPREFIX"), libname, suffix, env.subst("$SHLIBSUFFIX"))

    library = env.SharedLibrary("bin/{}/{}".format(env["platform"], lib_filename), source=sources)
    copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)
    Default(library, copy)
