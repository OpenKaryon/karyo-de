# SPDX-License-Identifier: GPL-3.0-or-later
"""Unprivileged command interface to the fixed polkit desktop helpers."""
import argparse
import os
from pathlib import PurePosixPath
import sys

from .model import account_name


def command(arguments, helper_directory):
    parser = argparse.ArgumentParser(prog="karyo-desktops", allow_abbrev=False)
    operations = parser.add_subparsers(dest="operation", required=True)
    listing = operations.add_parser("list", allow_abbrev=False, help="list your account family as JSON")
    listing.add_argument("--json", action="store_true", help="explicitly select the default JSON output")
    switching = operations.add_parser("switch", allow_abbrev=False, help="switch to an existing family session")
    switching.add_argument("name", type=account_name)
    adding = operations.add_parser("add", allow_abbrev=False, help="create a child of your main account")
    adding.add_argument("name", nargs="?", type=account_name, default=None)
    adding.add_argument("--label", default="")
    adding.add_argument("--protection", choices=("none", "home", "separate"), default="none")
    adding.add_argument("--password-stdin", action="store_true", help="read a separate password from stdin only")
    args = parser.parse_args(arguments)
    if args.operation == "list":
        parameters = []
    elif args.operation == "switch":
        parameters = [args.name]
    else:
        if args.password_stdin != (args.protection == "separate"):
            parser.error("--password-stdin is required only for separate password protection")
        parameters = ([args.name] if args.name else []) + ["--label=" + args.label, "--protection=" + args.protection]
    # This directory is a literal configured at package build time, not a CLI,
    # environment, home directory or current-working-directory preference.
    directory = PurePosixPath(helper_directory)
    if not directory.is_absolute() or ".." in directory.parts:
        raise ValueError("Invalid installed desktop helper directory")
    return ["/usr/bin/pkexec", str(directory / ("desktop-" + args.operation)), *parameters]


def main(helper_directory):
    try:
        arguments = command(sys.argv[1:], helper_directory)
        # Replacing this ordinary-user process preserves native pkexec's caller
        # session, stdin-only password and exact helper result/exit status.
        os.execv(arguments[0], arguments)
    except (OSError, ValueError):
        print("karyo-desktops: Could not start the installed desktop operation.", file=sys.stderr)
        return 1
