// build.zig
const std = @import("std");

const cpp_flags = [_][]const u8{
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-fno-sanitize=undefined",
};

const lib_sources = [_][]const u8{
    "src/lib/hash_table.cpp",
    "src/lib/file_backed_buffer.cpp",
};

pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed.
    const target = b.standardTargetOptions(.{});

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const mode = b.standardOptimizeOption(.{});

    // Build and install the main library
    const lib = buildLib(b, target, mode);

    // Build and install tests of the main library
    buildTests(b, lib, target, mode);
}

pub fn buildLib(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) *std.Build.Step.Compile {
    const lib = b.addSharedLibrary(.{
        .name = "key_value_store",
        .target = target,
        .optimize = optimize,
    });
    lib.addCSourceFiles(.{ .files = &lib_sources, .flags = &cpp_flags });
    lib.addIncludePath(b.path("src/lib/"));
    lib.linkLibC();
    lib.linkLibCpp();
    b.installArtifact(lib);
    return lib;
}

pub fn buildTests(
    b: *std.Build,
    dep: *std.Build.Step.Compile,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) void {
    const basic_test = b.addExecutable(.{
        .name = "kv_basic_test",
        .target = target,
        .optimize = optimize,
    });
    basic_test.addCSourceFile(.{ .file = b.path("src/tester/basic_test.cpp") });
    basic_test.addIncludePath(b.path("src/lib/"));
    basic_test.linkLibrary(dep);
    b.installArtifact(basic_test);

    // Create the stress test of the library
    const stress_test = b.addExecutable(.{
        .name = "kv_stress_test",
        .target = target,
        .optimize = optimize,
    });
    stress_test.addCSourceFile(.{ .file = b.path("src/tester/stress_test.cpp") });
    stress_test.addIncludePath(b.path("src/lib/"));
    stress_test.linkLibrary(dep);
    b.installArtifact(stress_test);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    const run_cmd = b.addRunArtifact(stress_test);

    // By making the run step depend on the install step, it will be run from the
    // installation directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed
    // files, this ensures they will be present and in the expected location.
    run_cmd.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build
    // command itself, like this: `zig build run -- arg1 arg2 etc`
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    // This creates a build step. It will be visible in the `zig build --help` menu,
    // and can be selected like this: `zig build run`
    // This will evaluate the `run` step rather than the default, which is "install".
    const run_step = b.step("run", "Run the stress test");
    run_step.dependOn(&run_cmd.step);
}
