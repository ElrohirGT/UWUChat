const std = @import("std");

const facilio_files = .{
    // Facilio core
    "fio.c",
    "fiobj/fio_siphash.c",
    "fiobj/fiobj_ary.c",
    "fiobj/fiobj_data.c",
    "fiobj/fiobj_hash.c",
    "fiobj/fiobj_json.c",
    "fiobj/fiobj_mustache.c",
    "fiobj/fiobj_numbers.c",
    "fiobj/fiobj_str.c",
    "fiobj/fiobject.c",

    // HTTP extensions
    "http/http.c",
    "http/http1.c",
    "http/http_internal.c",
    "http/websockets.c",

    // CLI extensions
    "cli/fio_cli.c",

    // TLS extensions
    "tls/fio_tls_missing.c",
    "tls/fio_tls_openssl.c",

    // Redis engine
    "redis/redis_engine.c",
};

const facilio_includes = [_][]const u8{
    "lib/facil",
    "lib/facil/fiobj",
    "lib/facil/http",
    "lib/facil/http/parsers",
    "lib/facil/cli",
    "lib/facil/tls",
    "lib/facil/redis",
};

const facilio_examples = [_][]const u8{
    "examples/http-chat.c",
    "examples/http-client.c",
    "examples/http-hello.c",
};

const c_compile_flags = [_][]const u8{"-Weverything"};

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) !void {
    // var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    // defer {
    //     switch (gpa.deinit()) {
    //         .leak => std.debug.print("LEAKED MEMORY!", .{}),
    //         .ok => std.debug.print("NO LEAKS!", .{}),
    //     }
    // }
    //
    // var alloc = std.heap.ArenaAllocator.init(gpa.allocator());
    // defer alloc.deinit();
    //
    // const arena = alloc.allocator();

    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const facilio_dep = b.dependency("facilio", .{
        .target = target,
        .optimize = optimize,
    });
    const facilio = try build_facilio(b, facilio_dep, target, optimize);

    var lib_paths = get_from_env: {
        var env = try std.process.getEnvMap(b.allocator);
        defer env.deinit();

        const nix_hook_contents = env.get("shellHook").?;
        var seq = std.mem.splitSequence(u8, nix_hook_contents, " ");
        const paths = seq.next().?;

        break :get_from_env std.mem.splitSequence(u8, paths, ":");
    };

    const raylib_dep = b.dependency("raylib", .{
        .target = target,
        .optimize = optimize,
        // .linux_display_backend = .X11,
        .shared = true,
    });
    const raylib = raylib_dep.artifact("raylib");

    const clay_example_exe = b.addExecutable(.{
        .name = "clay_example",
        .target = target,
        .optimize = optimize,
    });
    clay_example_exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/devDocs/example.c" } });
    while (lib_paths.next()) |path| {
        clay_example_exe.addLibraryPath(.{ .cwd_relative = path });
    }
    clay_example_exe.linkLibC();
    clay_example_exe.linkLibrary(raylib);
    // clay_example_exe.linkLibrary(raylib);

    const clay_example_cmd = b.addRunArtifact(clay_example_exe);
    const clay_example_run = b.step("example:clay", "Run the clay example");
    clay_example_run.dependOn(&clay_example_cmd.step);

    // First we create the basic executable
    const exe = b.addExecutable(.{
        .name = "uwuchat_server",
        .target = target,
        .optimize = optimize,
    });
    // Since we need to use printf and stuff
    // We need the libC library.
    exe.linkLibC();
    exe.linkLibrary(facilio);
    // Finally we add the main.c file to our executable as a source file.
    exe.addCSourceFile(.{
        .file = .{ .cwd_relative = "src/server.c" },
        .flags = &c_compile_flags,
    });
    for (facilio_includes) |dep_path| {
        exe.addIncludePath(facilio_dep.path(dep_path));
    }

    for (facilio_examples) |example_path_in_dep| {
        var iter = std.mem.split(u8, example_path_in_dep, "/");
        _ = iter.next();
        const output_name = iter.next().?;
        const step_name = try std.fmt.allocPrint(
            std.heap.page_allocator,
            "example:{s}",
            .{output_name},
        );

        const example_exe = b.addExecutable(.{
            .name = output_name,
            .target = target,
            .optimize = optimize,
        });

        example_exe.linkLibC();
        example_exe.linkLibrary(facilio);
        example_exe.addCSourceFile(.{ .file = .{ .dependency = .{ .dependency = facilio_dep, .sub_path = example_path_in_dep } } });

        for (facilio_includes) |dep_path| {
            example_exe.addIncludePath(facilio_dep.path(dep_path));
        }

        const example_cmd = b.addRunArtifact(example_exe);
        const example_run_step = b.step(step_name, "Run the example from facilio");
        example_run_step.dependOn(&example_cmd.step);
    }

    // This declares intent for the executable to be installed into the
    // standard location when the user invokes the "install" step (the default
    // step when running `zig build`).
    b.installArtifact(exe);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    const run_cmd = b.addRunArtifact(exe);

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
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    // Creates a step for unit testing. This only builds the test executable
    // but does not run it.
    // const lib_unit_tests = b.addTest(.{
    //     .root_source_file = b.path("src/root.zig"),
    //     .target = target,
    //     .optimize = optimize,
    // });

    // const run_lib_unit_tests = b.addRunArtifact(lib_unit_tests);

    // const exe_unit_tests = b.addTest(.{
    //     .root_source_file = b.path("src/main.zig"),
    //     .target = target,
    //     .optimize = optimize,
    // });

    // const run_exe_unit_tests = b.addRunArtifact(exe_unit_tests);

    // Similar to creating the run step earlier, this exposes a `test` step to
    // the `zig build --help` menu, providing a way for the user to request
    // running the unit tests.
    // const test_step = b.step("test", "Run unit tests");
    // test_step.dependOn(&run_lib_unit_tests.step);
    // test_step.dependOn(&run_exe_unit_tests.step);
}

fn build_facilio(b: *std.Build, facilio_dep: *std.Build.Dependency, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) !*std.Build.Step.Compile {
    // This funciont is based on the Zap project.
    // Based on: https://github.com/zigzap/zap/blob/master/facil.io/build.zig

    var flags = std.ArrayList([]const u8).init(std.heap.page_allocator);
    if (optimize != .Debug) try flags.append("-Os");
    try flags.append("-Wno-return-type-c-linkage");
    try flags.append("-fno-sanitize=undefined");
    try flags.append("-DFIO_HTTP_EXACT_LOGGING");
    try flags.append("-DHAVE_OPENSSL");
    try flags.append("-DFIO_TLS_FOUND");

    if (target.result.abi == .musl)
        try flags.append("-D_LARGEFILE64_SOURCE");

    const facilio = b.addStaticLibrary(.{
        .name = "facilio",
        .link_libc = true,
        .target = target,
        .optimize = optimize,
    });
    facilio.addCSourceFiles(.{
        .root = facilio_dep.path("lib/facil"),
        .files = &facilio_files,
        .flags = flags.items,
    });
    for (facilio_includes) |dep_path| {
        facilio.addIncludePath(facilio_dep.path(dep_path));
    }
    facilio.addIncludePath(facilio_dep.path(""));
    facilio.linkLibC();
    facilio.linkSystemLibrary("ssl");
    facilio.linkSystemLibrary("crypto");
    b.installArtifact(facilio);

    return facilio;
}
