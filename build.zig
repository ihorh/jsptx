const std = @import("std");
const jzbuild = @import("jzbuild");

pub fn build(b: *std.Build) void {
    _ = jzbuild.app(b, .{ .name = "jsptx" });
}
