#!/usr/bin/env python3
"""
LoDB Protobuf Generator.

A generalized script to generate protobuf C++ files using nanopb.
Works standalone or as part of a build system (PlatformIO/SCons).

Usage (Standalone):
    python gen_proto.py path/to/*.proto

Usage (PlatformIO/SCons):
    Import and call generate_protobuf_files() with appropriate paths,
    or use generate_protobufs() to obtain an SCons pre-action.

Requirements:
    - nanopb 0.4.9+ (with generator-bin/protoc)
    - Python 3.x
"""
import argparse
import glob
import os
import subprocess
import sys

try:
    from SCons.Script import Import as scons_import  # type: ignore
except Exception:  # pragma: no cover - SCons not available outside build
    scons_import = None

# Allow importing when consumed as a submodule from firmware projects
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def find_nanopb_dir(start_dir=None):
    """
    Find the nanopb directory by searching upward from start_dir.

    Args:
        start_dir: Directory to start search from (defaults to script directory).

    The function returns the path to the nanopb directory or None if not found.

    """

    if start_dir is None:
        start_dir = os.path.dirname(os.path.abspath(__file__))

    current = start_dir
    # Search up to 5 levels up
    for _ in range(5):
        # Check for nanopb-0.4.9 or nanopb directory
        for dirname in ["nanopb-0.4.9", "nanopb"]:
            candidate = os.path.join(current, dirname)
            if os.path.exists(candidate) and os.path.isdir(candidate):
                # Verify it has the generator
                protoc_path = os.path.join(candidate, "generator-bin", "protoc")
                if sys.platform.startswith("win"):
                    protoc_path += ".exe"
                if os.path.exists(protoc_path):
                    return candidate

        # Go up one level
        parent = os.path.dirname(current)
        if parent == current:
            break  # Reached root
        current = parent

    return None


def _expand_with_env(env, value):
    if value is None:
        return None
    if env is not None and hasattr(env, "subst"):
        return env.subst(value)
    return value


def generate_protobuf_files(
    proto_file, options_file=None, output_dir=None, nanopb_dir=None
):
    """
    Generate protobuf C++ files using nanopb.

    Args:
        proto_file: Path to .proto file (required)
        options_file: Path to .options file (optional, auto-detected if in same dir)
        output_dir: Output directory for generated files (optional, defaults to proto dir)
        nanopb_dir: Path to nanopb directory (optional, auto-detected)

    Returns True if generation succeeds, otherwise False.

    """

    # Resolve proto file path
    proto_file = os.path.abspath(proto_file)
    if not os.path.exists(proto_file):
        print(f"Error: Proto file not found: {proto_file}")
        return False

    # Get proto directory and filename
    proto_dir = os.path.dirname(proto_file)
    proto_basename = os.path.basename(proto_file)
    proto_name = os.path.splitext(proto_basename)[0]

    # Determine output directory
    if output_dir is None:
        output_dir = proto_dir
    else:
        output_dir = os.path.abspath(output_dir)

    # Create output directory if needed
    os.makedirs(output_dir, exist_ok=True)

    # Auto-detect options file if not specified
    if options_file is None:
        candidate_options = os.path.join(proto_dir, f"{proto_name}.options")
        if os.path.exists(candidate_options):
            options_file = candidate_options
            print(f"Auto-detected options file: {options_file}")
    elif options_file:
        options_file = os.path.abspath(options_file)
        if not os.path.exists(options_file):
            print(f"Warning: Options file not found: {options_file}")
            options_file = None

    # Find nanopb directory
    if nanopb_dir is None:
        nanopb_dir = find_nanopb_dir(proto_dir)
        if nanopb_dir is None:
            print("Error: Could not find nanopb directory")
            print(
                "Please specify --nanopb-dir or ensure nanopb is in parent directories"
            )
            return False
    else:
        nanopb_dir = os.path.abspath(nanopb_dir)

    print(f"Using nanopb directory: {nanopb_dir}")

    # Determine protoc executable based on platform
    if sys.platform.startswith("win"):
        protoc_exe = os.path.join(nanopb_dir, "generator-bin", "protoc.exe")
    else:
        protoc_exe = os.path.join(nanopb_dir, "generator-bin", "protoc")

    if not os.path.exists(protoc_exe):
        print(f"Error: protoc executable not found at {protoc_exe}")
        return False

    # Check if generated files already exist and are up to date
    pb_header = os.path.join(output_dir, f"{proto_name}.pb.h")
    pb_source = os.path.join(output_dir, f"{proto_name}.pb.cpp")

    if os.path.exists(pb_header) and os.path.exists(pb_source):
        proto_mtime = os.path.getmtime(proto_file)
        header_mtime = os.path.getmtime(pb_header)
        source_mtime = os.path.getmtime(pb_source)

        options_mtime = os.path.getmtime(options_file) if options_file else None

        if header_mtime > proto_mtime and source_mtime > proto_mtime:
            if options_mtime is None or (
                header_mtime > options_mtime and source_mtime > options_mtime
            ):
                print(f"Protobuf files for {proto_basename} are up to date")
                return True

    # Generate protobuf files
    print("=" * 60)
    print(f"Generating protobuf files from {proto_basename}...")
    print("=" * 60)

    # Build the protoc command
    # nanopb output format: -S.cpp generates .cpp instead of .c
    # -v is verbose
    cmd = [
        protoc_exe,
        "--experimental_allow_proto3_optional",
        f"--nanopb_out=-S.cpp -v:{output_dir}",
        f"-I={proto_dir}",
        proto_file,
    ]

    print(f"Command: {' '.join(cmd)}")

    try:
        # Change to proto directory so nanopb can find the .options file
        original_dir = os.getcwd()
        os.chdir(proto_dir)

        result = subprocess.run(cmd, check=True, capture_output=True, text=True)

        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)

        print(f"✓ Generated {pb_header}")
        print(f"✓ Generated {pb_source}")
        print("=" * 60)

        return True

    except subprocess.CalledProcessError as e:
        print(f"Error generating protobufs: {e}")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        return False
    finally:
        os.chdir(original_dir)


def generate_protobufs(proto_path, options_path=None, output_dir=None, nanopb_dir=None):
    """Return an SCons-compatible callable that generates protobufs for proto_path."""

    def _action(source=None, target=None, env=None):  # noqa: ARG001 - SCons signature
        resolved_proto = _expand_with_env(env, proto_path)
        resolved_options = _expand_with_env(env, options_path)
        resolved_output = _expand_with_env(env, output_dir)
        resolved_nanopb = _expand_with_env(env, nanopb_dir)
        return generate_protobuf_files(
            proto_file=resolved_proto,
            options_file=resolved_options,
            output_dir=resolved_output,
            nanopb_dir=resolved_nanopb,
        )

    return _action


def register_protobufs(
    env, object_target, proto_path, options_path=None, output_dir=None, nanopb_dir=None
):
    """Register a pre-build action to regenerate protobufs for a module."""

    if env is None:
        raise ValueError("env is required to register protobuf generation")

    action = generate_protobufs(proto_path, options_path, output_dir, nanopb_dir)

    if isinstance(object_target, (list, tuple)):
        for target in object_target:
            env.AddPreAction(target, action)
    else:
        env.AddPreAction(object_target, action)

    print(f"Registered protobuf generation action for {object_target}")

    return action


def main():
    """Command-line interface for standalone usage."""
    parser = argparse.ArgumentParser(
        description="Generate protobuf C++ files using nanopb",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate from proto files in current directory
  python gen_proto.py *.proto

  # Generate from another directory
  python gen_proto.py ../modules/LoBBSModule/*.proto

  # Specify output directory
  python gen_proto.py myschema.proto --output-dir build/

  # Specify nanopb directory
  python gen_proto.py myschema.proto --nanopb-dir /path/to/nanopb-0.4.9
        """,
    )

    parser.add_argument(
        "proto_paths",
        nargs="+",
        help="Proto file paths or glob patterns (required)",
    )

    parser.add_argument(
        "--options",
        help="Path to .options file (optional, applies to all matches; auto-detected per proto otherwise)",
    )

    parser.add_argument(
        "--output-dir",
        help="Output directory for generated files (optional, defaults to proto directory)",
    )

    parser.add_argument(
        "--nanopb-dir",
        help="Path to nanopb directory (optional, auto-detected by searching parent directories)",
    )

    args = parser.parse_args()

    # Expand glob patterns
    matched_protos = []
    for pattern in args.proto_paths:
        expanded = glob.glob(pattern)
        if expanded:
            matched_protos.extend(expanded)
        else:
            print(f"Warning: pattern did not match any files: {pattern}")

    if not matched_protos:
        print("Error: no proto files found")
        sys.exit(1)

    success = True
    for proto in matched_protos:
        proto_success = generate_protobuf_files(
            proto_file=proto,
            options_file=args.options,
            output_dir=args.output_dir,
            nanopb_dir=args.nanopb_dir,
        )
        success = success and proto_success

    sys.exit(0 if success else 1)


# PlatformIO/SCons integration
if scons_import is not None:
    try:
        # When imported by platformio-custom.py or build script
        scons_import("env")

        # This section would be customized by the importing module
        # Example usage in your module's build script:
        #
        # import sys
        # sys.path.append('src/lodb')
        # from gen_proto import register_protobufs
        #
        # register_protobufs(
        #     env,
        #     "$BUILD_DIR/src/mymodule/MyModule.cpp.o",
        #     "$PROJECT_DIR/src/mymodule/myschema.proto",
        # )

        print("gen_proto.py loaded for build integration")

    except Exception:
        # Not running as an SConscript, this is normal for standalone execution
        pass


if __name__ == "__main__":
    main()
