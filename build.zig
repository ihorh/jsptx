const std = @import("std");
const jzbuild = @import("jzbuild");

const Classify = enum { auto, scalar };

pub fn build(b: *std.Build) void {
    const classify = b.option(
        Classify,
        "classify",
        "force jsp_classify64 to one implementation, bypassing SIMD (default: auto)",
    ) orelse .auto;

    const exe = jzbuild.app(b, .{ .name = "jsptx" });
    switch (classify) {
        .auto => {},
        .scalar => exe.root_module.addCMacro("JSP_FORCE_SCALAR", "1"),
    }

    addBench(b, exe);
}

/// jzbuild's app() wires src/, include/, and tests/ but has no bench/
/// convention of its own yet, so this is hand-rolled: one executable per
/// bench/*_bench.c, compiled against the app's own sources (minus main.c,
/// same as a test binary), wired to a `bench` step. Measurement, not
/// verification — never part of `test`.
fn addBench(b: *std.Build, exe: *std.Build.Step.Compile) void {
    const io = b.graph.io;
    var dir = b.build_root.handle.openDir(io, "bench", .{ .iterate = true }) catch return;
    defer dir.close(io);

    const sources = appSourcesWithoutMain(b);
    const bench_step = b.step("bench", "run jsptx measurements");

    var it = dir.iterate();
    while (it.next(io) catch @panic("cannot read bench/")) |entry| {
        if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, "_bench.c")) continue;

        const bench_src = b.pathJoin(&.{ "bench", entry.name });
        const bench_exe = b.addExecutable(.{
            .name = std.fs.path.stem(entry.name),
            .root_module = b.createModule(.{
                .target = exe.root_module.resolved_target,
                .optimize = exe.root_module.optimize,
                .link_libc = true,
            }),
        });
        bench_exe.root_module.addIncludePath(b.path("include"));
        bench_exe.root_module.addCSourceFiles(.{ .files = sources });
        bench_exe.root_module.addCSourceFiles(.{ .files = &.{bench_src} });
        bench_step.dependOn(&b.addRunArtifact(bench_exe).step);
    }
}

/// src/*.c, minus main.c: the same source set a jzbuild test binary compiles
/// against, so a bench reaches any function in the project without linking
/// against the installed app itself.
fn appSourcesWithoutMain(b: *std.Build) []const []const u8 {
    const io = b.graph.io;
    var dir = b.build_root.handle.openDir(io, "src", .{ .iterate = true }) catch
        @panic("bench/ exists but src/ does not");

    var found: std.ArrayList([]const u8) = .empty;
    var it = dir.iterate();
    while (it.next(io) catch @panic("cannot read src/")) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.name, ".c")) continue;
        if (std.mem.eql(u8, entry.name, "main.c")) continue;
        found.append(b.allocator, b.pathJoin(&.{ "src", entry.name })) catch @panic("OOM");
    }
    dir.close(io);
    return found.toOwnedSlice(b.allocator) catch @panic("OOM");
}
