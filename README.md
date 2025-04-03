# UwuChat

<h2>
Please read the instructions carefully!
</h2>

## Cloning this repository

This project uses
[git submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules) to manage
its primary code dependencies!

This means you need to clone it in a special way:

```bash
# Clone the repository and it's dependencies
git submodule update --init --recursive
```

This may take a while depending on your internet connection, so go make a coffe!

## Nix

This project uses Nix for it's dependencies, please install it from
[NixOS Oficial](https://nixos.org/) and make sure you enable
[Flakes](https://wiki.nixos.org/wiki/Flakes).

All commands below should be run in the shell that nix creates! To create this
shell use:

```bash
# On the project root
nix develop

# If you DON'T enable flakes, please user this command:
nix develop --experimental-features 'nix-command flakes'
```

This command will provide you an environment with all the necessary dependencies
to compile and run our project.

## Compile and run the backend

NOTE: Remember to enter the Nix shell described in the [Nix section](#Nix).

To compile and run the backend, please use:

```bash
# Customize the bindings and port as you please!
zig build run -- -b 127.0.0.1 -p 8080
```

## Compile and run the frontend

NOTE: Remember to enter the Nix shell described in the [Nix section](#Nix).

To compile and run the frontend, please use:

```bash
# Make sure to end the URL with '/'!
# Customize the username and URL as you see fit!
zig build client -- FitGirlLover69 ws://127.0.0.1:8080/
```

In some systems the dependencies installed by Nix won't work because of NVIDIA
or other factors, if this is the case we have a CMAKE fallback courtesy of
@DanielRasho.

```bash
# CMAKE setup
mkdir build
cd build
cmake ..

# Build project
cmake --build .
# Run project
./uwu_client FitGirlLover69 ws://127.0.0.1:8080/
```

## Zig build system

Is kinda complicated to modify/build stuff if you don't know anything about the
build system. But as a user is really simple to use!

```bash
# To see which commands are available
zig build --help
```
