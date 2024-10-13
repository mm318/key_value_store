// build.zig
const std = @import("std");

const cpp_flags = [_][]const u8{
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-fno-sanitize=undefined",
};

pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed.
    const target = b.standardTargetOptions(.{});

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addStaticLibrary(.{
        .name = "fpng",
        .target = target,
        .optimize = optimize,
    });
    lib.addCSourceFile(.{ .file = b.path("src/fpng.cpp"), .flags = &cpp_flags });
    lib.addIncludePath(b.path("src/"));
    lib.linkLibC();
    lib.linkLibCpp();

    b.installArtifact(lib);
}
