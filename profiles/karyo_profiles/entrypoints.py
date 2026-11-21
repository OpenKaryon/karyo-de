# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed privileged operations; no caller-selected owner, path or executable."""
import argparse
import json
import os
import select
import sys
import time

from .credentials import AccountOperationError, LocalAccounts, PasswordSync
from .logind import SessionError, Sessions, authorized_caller, listing
from .model import account_name
from .provision import AccountProvisioner, CreationError
from .storage import ConfigStore
from .switching import DesktopStopped, ExistingSwitcher
from .runtime_policy import enabled as runtime_enabled
from .dispatcher import request as host_request


def read_password(fd=0, *, timeout=10):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            raise TimeoutError("The new desktop password input did not finish")
        block = os.read(fd, min(512, 1026 - len(data)))
        if not block:
            break
        data.extend(block)
        if len(data) > 1025:
            raise ValueError("The new password exceeds its limit")
    if not data.endswith(b"\n"):
        raise ValueError("A new password must be one complete input line")
    password = data[:-1].decode("utf-8")
    if not password or any(c in password for c in "\0\r\n"):
        raise ValueError("A new password must be one nonempty input line")
    return password


def add(arguments, *, environment, store, authorize=authorized_caller, provision=AccountProvisioner,
        password_input=read_password):
    parser = argparse.ArgumentParser(prog="karyo-desktop-add", allow_abbrev=False)
    parser.add_argument("name", nargs="?", default="")
    parser.add_argument("--label", default="")
    parser.add_argument("--protection", choices=("none", "home", "separate"), default="none")
    args = parser.parse_args(arguments)
    caller = authorize(environment)
    password = password_input() if args.protection == "separate" else None
    account = provision(store).add(caller.uid, args.label or args.name, name=args.name,
                                   protection=args.protection, password=password)
    return {"user": account.name}


def list_desktops(arguments, *, environment, store, authorize=authorized_caller,
                  accounts=LocalAccounts, sessions=Sessions):
    if arguments:
        raise ValueError("Desktop listing accepts no owner, path or session arguments")
    caller = authorize(environment)
    with store.locked():
        return listing(caller, store, accounts(), sessions())


def switch_existing(arguments, *, environment, store, authorize=authorized_caller,
                    switcher=ExistingSwitcher, runtime=runtime_enabled, request=host_request):
    parser = argparse.ArgumentParser(prog="karyo-desktop-switch", allow_abbrev=False)
    parser.add_argument("name", type=account_name)
    args = parser.parse_args(arguments)
    caller = authorize(environment)
    if runtime():
        # Local pkexec authorization is still required. The host independently
        # derives this root process's native login account; no caller UID travels.
        return request({"version": 1, "operation": "switch", "name": args.name})
    return switcher(store).switch(caller, args.name)


def execute(operation, arguments, *, environment=None, store=None):
    environment = os.environ if environment is None else environment
    store = ConfigStore() if store is None else store
    if operation == "add":
        return add(arguments, environment=environment, store=store)
    if operation == "list":
        return list_desktops(arguments, environment=environment, store=store)
    if operation == "switch":
        return switch_existing(arguments, environment=environment, store=store)
    if operation in {"sync", "password-changed"}:
        if arguments:
            raise ValueError("This system operation accepts no arguments")
        synchronization = PasswordSync(store)
        if operation == "sync":
            synchronization.all(environment=environment)
        else:
            synchronization.after_password_change(environment=environment)
        return None
    raise ValueError("Unsupported privileged desktop operation")


def main(operation):
    try:
        result = execute(operation, sys.argv[1:])
        if result is not None:
            print(json.dumps(result))
        return 0
    except DesktopStopped as error:
        print("karyo-desktop: " + str(error), file=sys.stderr)
        return 3
    except CreationError as error:
        print("karyo-desktop: " + str(error), file=sys.stderr)
        return 2 if error.retained else 1
    except UnicodeError:
        print("karyo-desktop: Password input must use valid UTF-8.", file=sys.stderr)
        return 1
    except (PermissionError, ValueError, TimeoutError, AccountOperationError, SessionError) as error:
        print("karyo-desktop: " + str(error), file=sys.stderr)
        return 1
    except (OSError, RuntimeError):
        # Native diagnostics and tracebacks could include privileged account data.
        print("karyo-desktop: The desktop operation could not finish; administrator review is required.", file=sys.stderr)
        return 1
