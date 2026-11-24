import argparse
import os
import struct
import subprocess
import sys

FLASH_BASE = 0x08000000
FLASH_TOTAL_SIZE = 0x8000      # 32 KB (GD32F130G6)
EEPROM_RESERVE_SIZE = 0x400    # Last 1 KB page reserved for emulated EEPROM


def merge_files(bl_path, app_bin_path, combined_bin_path, app_offset=0x2000):
    if not os.path.isfile(bl_path):
        sys.stderr.write("[ERROR] Bootloader binary not found: %s\n" % bl_path)
        return False

    if not os.path.isfile(app_bin_path):
        sys.stderr.write("[ERROR] Application binary not found: %s\n" % app_bin_path)
        return False

    with open(bl_path, "rb") as f:
        bl_data = f.read()

    with open(app_bin_path, "rb") as f:
        app_data = f.read()

    if len(bl_data) > app_offset:
        sys.stderr.write(
            "[ERROR] Bootloader size (%d bytes) exceeds app offset (0x%X = %d bytes)\n"
            % (len(bl_data), app_offset, app_offset)
        )
        return False

    if len(app_data) < 8:
        sys.stderr.write("[ERROR] Application binary is too small (%d bytes)\n" % len(app_data))
        return False

    # Sanity-check ARM vector table so we never accidentally merge a 0-offset (_develop) build
    msp, reset_vec = struct.unpack_from("<II", app_data, 0)
    expected_app_addr = FLASH_BASE + app_offset
    if (msp >> 20) != 0x200 or not (expected_app_addr <= reset_vec < FLASH_BASE + FLASH_TOTAL_SIZE):
        sys.stderr.write(
            "[ERROR] App vector table looks invalid for offset 0x%X "
            "(MSP=0x%08X, Reset_Handler=0x%08X). "
            "Make sure this env inherits a target with -Wl,--defsym=FLASH_APP_OFFSET=8K "
            "(not a *_develop env).\n" % (app_offset, msp, reset_vec)
        )
        return False

    total_size = app_offset + len(app_data)
    max_app_end = FLASH_TOTAL_SIZE - EEPROM_RESERVE_SIZE
    if total_size > FLASH_TOTAL_SIZE:
        sys.stderr.write(
            "[ERROR] Combined image (%d bytes) exceeds 32 KB Flash (%d bytes)!\n"
            % (total_size, FLASH_TOTAL_SIZE)
        )
        return False
    elif total_size > max_app_end:
        sys.stderr.write(
            "[WARNING] Combined image (%d bytes) overlaps last 1 KB EEPROM page (0x%08X)!\n"
            % (total_size, FLASH_BASE + max_app_end)
        )

    padded_bl = bl_data + b"\xFF" * (app_offset - len(bl_data))
    combined_data = padded_bl + app_data

    with open(combined_bin_path, "wb") as f:
        f.write(combined_data)

    print(
        "Merged bootloader (%d B @ 0x%08X) + app (%d B @ 0x%08X) -> %s (%d B)"
        % (
            len(bl_data),
            FLASH_BASE,
            len(app_data),
            expected_app_addr,
            combined_bin_path,
            len(combined_data),
        )
    )
    return True


def _run_openocd(openocd_exe, scripts_dir, commands, cwd):
    args = [openocd_exe]
    if scripts_dir:
        args.extend(["-s", scripts_dir])
    args.extend([
        "-f", "interface/stlink.cfg",
        "-c", "transport select hla_swd",
        "-f", "target/stm32f1x.cfg",
    ])
    for cmd in commands:
        args.extend(["-c", cmd])

    print("Running:", " ".join(args))
    res = subprocess.run(args, cwd=cwd)
    return res.returncode


def _cli_main():
    parser = argparse.ArgumentParser(
        description="Merge OpenVTx bootloader + app binary and optionally flash via ST-Link OpenOCD."
    )
    parser.add_argument("--bootloader", required=True, help="Path to bootloader .bin")
    parser.add_argument("--app", required=True, help="Path to app firmware .bin")
    parser.add_argument("--output", required=True, help="Path to output combined .bin")
    parser.add_argument("--offset", default="0x2000", help="App flash offset (default: 0x2000)")
    parser.add_argument("--upload", action="store_true", help="Flash combined image via OpenOCD ST-Link")
    parser.add_argument("--unlock", action="store_true", help="Unlock RDP and mass erase before flashing")
    parser.add_argument("--openocd", default="openocd", help="Path to openocd executable")
    parser.add_argument("--scripts", default="", help="Path to openocd scripts directory")
    parser.add_argument("--cwd", default=os.getcwd(), help="Working directory for OpenOCD")
    args = parser.parse_args()

    app_offset = int(args.offset, 0)
    if not merge_files(args.bootloader, args.app, args.output, app_offset):
        return 1

    if not args.upload:
        return 0

    # Use relative forward-slash path to avoid OpenOCD TCL backslash / non-ASCII path issues
    rel_combined = os.path.relpath(args.output, args.cwd).replace("\\", "/")
    scripts_dir = args.scripts if args.scripts and os.path.isdir(args.scripts) else None

    if args.unlock:
        print("Unlocking flash read protection and performing mass erase...")
        rc = _run_openocd(
            args.openocd,
            scripts_dir,
            ["init; reset halt; stm32f1x unlock 0; reset halt; exit;"],
            args.cwd,
        )
        if rc != 0:
            return rc

        rc = _run_openocd(
            args.openocd,
            scripts_dir,
            ["init; reset halt; wait_halt; stm32f1x mass_erase 0; sleep 200; reset halt; exit;"],
            args.cwd,
        )
        if rc != 0:
            return rc

    print("Flashing combined image (bootloader + app) at 0x%08X via ST-Link..." % FLASH_BASE)
    return _run_openocd(
        args.openocd,
        scripts_dir,
        ["program {%s} 0x%08X verify reset exit" % (rel_combined, FLASH_BASE)],
        args.cwd,
    )


if __name__ == "__main__":
    sys.exit(_cli_main())
else:
    Import("env")

    def _get_bool_option(option_name, default=False):
        val = env.GetProjectOption(option_name, str(default)).strip().lower()
        return val in ("1", "true", "yes", "on")

    def _resolve_paths():
        project_dir = env.subst("$PROJECT_DIR")
        build_dir = env.subst("$BUILD_DIR")
        progname = env.subst("$PROGNAME")
        bl_rel = env.GetProjectOption(
            "custom_bootloader", "bootloader/Generic_GD32F130_bootloader.bin"
        )
        bl_path = bl_rel if os.path.isabs(bl_rel) else os.path.join(project_dir, bl_rel)
        app_path = os.path.join(build_dir, "%s.bin" % progname)
        combined_path = os.path.join(build_dir, "%s_combined.bin" % progname)
        app_offset = int(env.GetProjectOption("custom_bootloader_offset", "0x2000"), 0)
        return project_dir, bl_path, app_path, combined_path, app_offset

    def _find_openocd():
        platform = env.PioPlatform()
        pkg_dir = platform.get_package_dir("tool-openocd")
        if pkg_dir:
            exe_name = "openocd.exe" if sys.platform.startswith("win") else "openocd"
            openocd_exe = os.path.join(pkg_dir, "bin", exe_name)
            scripts_dir = os.path.join(pkg_dir, "scripts")
            if os.path.isfile(openocd_exe):
                return openocd_exe, scripts_dir if os.path.isdir(scripts_dir) else ""
        return "openocd", ""

    def post_build_merge(source, target, env):
        _, bl_path, app_path, combined_path, app_offset = _resolve_paths()
        if not merge_files(bl_path, app_path, combined_path, app_offset):
            env.Exit(1)

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", post_build_merge)

    if _get_bool_option("custom_stlink_flash", False):
        project_dir, bl_path, app_path, combined_path, app_offset = _resolve_paths()
        openocd_exe, scripts_dir = _find_openocd()
        rel_bl = os.path.relpath(bl_path, project_dir).replace("\\", "/")
        rel_app = os.path.relpath(app_path, project_dir).replace("\\", "/")
        rel_combined = os.path.relpath(combined_path, project_dir).replace("\\", "/")

        cmd_parts = [
            '"$PYTHONEXE"',
            "scripts/merge_bootloader.py",
            "--upload",
            '--bootloader "%s"' % rel_bl,
            '--app "%s"' % rel_app,
            '--output "%s"' % rel_combined,
            "--offset 0x%X" % app_offset,
            '--openocd "%s"' % openocd_exe,
        ]
        if scripts_dir:
            cmd_parts.append('--scripts "%s"' % scripts_dir)
        if _get_bool_option("custom_stlink_unlock", False):
            cmd_parts.append("--unlock")

        env.Replace(UPLOADCMD=" ".join(cmd_parts))
