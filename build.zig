const std = @import("std");

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    // const lib = b.addStaticLibrary(.{
    //     .name = "UwuChat",
    //     // In this case the main source file is merely a path, however, in more
    //     // complicated build scripts, this could be a generated file.
    //     .root_source_file = b.path("src/root.zig"),
    //     .target = target,
    //     .optimize = optimize,
    // });

    // This declares intent for the library to be installed into the standard
    // location when the user invokes the "install" step (the default step when
    // running `zig build`).
    // b.installArtifact(lib);
    //

    const facilio_dep = b.dependency("facilio", .{
        .target = target,
        .optimize = optimize,
    });

    const facilio = b.addStaticLibrary(.{
        .name = "facilio",
        .link_libc = true,
        .target = target,
        .optimize = optimize,
    });
    facilio.addCSourceFiles(.{
        .root = facilio_dep.path("lib/facil"),
        .files = &.{
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
        },
    });
    facilio.addIncludePath(facilio_dep.path("lib/facil"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/fiobj"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/http"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/http/parsers"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/cli"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/tls"));
    facilio.addIncludePath(facilio_dep.path("lib/facil/redis"));

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
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/main.c" } });
    exe.addIncludePath(facilio_dep.path("lib/facil"));
    exe.addIncludePath(facilio_dep.path("lib/facil/fiobj"));
    exe.addIncludePath(facilio_dep.path("lib/facil/http"));
    exe.addIncludePath(facilio_dep.path("lib/facil/http/parsers"));
    exe.addIncludePath(facilio_dep.path("lib/facil/cli"));
    exe.addIncludePath(facilio_dep.path("lib/facil/tls"));
    exe.addIncludePath(facilio_dep.path("lib/facil/redis"));

    const chat_exe = b.addExecutable(.{
        .name = "chat_example",
        .target = target,
        .optimize = optimize,
    });
    chat_exe.linkLibC();
    chat_exe.linkLibrary(facilio);
    chat_exe.addIncludePath(facilio_dep.path("lib/facil"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/fiobj"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/http"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/http/parsers"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/cli"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/tls"));
    chat_exe.addIncludePath(facilio_dep.path("lib/facil/redis"));
    chat_exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/http-client.c" } });

    b.installArtifact(chat_exe);

    const chat_example_cmd = b.addRunArtifact(chat_exe);
    chat_example_cmd.step.dependOn(b.getInstallStep());
    const chat_example_run_step = b.step("run-chat", "Run the WS Chat example from the facilio library");
    chat_example_run_step.dependOn(&chat_example_cmd.step);

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
