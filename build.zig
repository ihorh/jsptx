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
}
