# SPDX-License-Identifier: GPL-3.0-or-later
"""Root-installed fixed worker declarations; no runtime executable arguments."""
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat

from .journal import unique_object
from .model import root_file
from .services import program_identity
from .storage import trusted_directory
from .worker_resources import Resources

DIRECTORY = Path('/usr/share/karyo/desktop-workers')
IDENTIFIER = re.compile(r'[a-z][a-z0-9]*(?:\.[A-Za-z][A-Za-z0-9_-]*){2,7}')


@dataclass(frozen=True)
class Declaration:
    identifier: str
    revision: str
    arguments: tuple
    recovery_arguments: tuple
    resources: Resources
    capabilities: tuple
    ready_seconds: int
    cleanup_seconds: int
    stop_application: bool

    @classmethod
    def parse(cls, text):
        if not isinstance(text, str) or len(text.encode('utf-8')) > 65536:
            raise ValueError('Worker declaration exceeds its bound')
        value = json.loads(text, object_pairs_hook=unique_object)
        fields = {'version', 'id', 'revision', 'argv', 'capabilities',
                  'ready_seconds', 'cleanup_seconds', 'stop_application', 'recover_argv', 'resources'}
        if not isinstance(value, dict) or set(value) != fields or type(value['version']) is not int or value['version'] != 1:
            raise ValueError('Unsupported worker declaration schema')
        identifier, revision = value['id'], value['revision']
        if not isinstance(identifier, str) or len(identifier) > 160 or IDENTIFIER.fullmatch(identifier) is None:
            raise ValueError('Invalid package worker identifier')
        if (not isinstance(revision, str) or not 1 <= len(revision) <= 128
                or any(ord(letter) < 33 or ord(letter) > 126 for letter in revision)):
            raise ValueError('Invalid worker package revision')
        def arguments(key):
            rows = value[key]
            if (not isinstance(rows, list) or not 1 <= len(rows) <= 16
                    or any(not isinstance(arg, str) or len(arg.encode('utf-8')) > 4096
                           or any(letter in arg for letter in '\0\r\n') for arg in rows)):
                raise ValueError('Invalid fixed worker argument vector')
            executable = Path(rows[0])
            if not executable.is_absolute() or '..' in executable.parts:
                raise ValueError('The package executable must be an absolute installed path')
            return tuple(rows)
        launch, recovery = arguments('argv'), arguments('recover_argv')
        resources = Resources.parse(value['resources'])
        capabilities = value['capabilities']
        if (not isinstance(capabilities, list) or len(capabilities) > 2
                or any(cap not in ('CAP_NET_ADMIN', 'CAP_NET_RAW') for cap in capabilities)
                or len(set(capabilities)) != len(capabilities)):
            raise ValueError('Unsupported worker capability set')
        for key, maximum in (('ready_seconds', 30), ('cleanup_seconds', 180)):
            if type(value[key]) is not int or not 1 <= value[key] <= maximum:
                raise ValueError('Invalid bounded worker deadline')
        if type(value['stop_application']) is not bool:
            raise ValueError('Worker application retirement policy must be explicit')
        return cls(identifier, revision, launch, recovery, resources, tuple(sorted(capabilities)),
                   value['ready_seconds'], value['cleanup_seconds'], value['stop_application'])


@dataclass(frozen=True)
class InstalledDeclaration:
    declaration: Declaration
    digest: str
    executable: tuple
    recovery_executable: tuple
    directory: Path

    @classmethod
    def capture(cls, identifier, *, directory=DIRECTORY):
        if not isinstance(identifier, str) or IDENTIFIER.fullmatch(identifier) is None:
            raise ValueError('A package worker identifier is required')
        directory = Path(directory)
        parent = trusted_directory(directory)
        try:
            name = identifier + '.json'
            text = root_file(name, dir_fd=parent)
            info = os.stat(name, dir_fd=parent, follow_symlinks=False)
            if info.st_gid != 0 or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o644:
                raise ValueError('Worker declarations require root-only write ownership')
        finally:
            os.close(parent)
        declaration = Declaration.parse(text)
        if declaration.identifier != identifier:
            raise ValueError('The installed worker identifier changed')
        return cls(declaration, hashlib.sha256(text.encode('utf-8')).hexdigest(),
                   program_identity(declaration.arguments[0]),
                   program_identity(declaration.recovery_arguments[0]), directory)

    def verify(self):
        if type(self).capture(self.declaration.identifier, directory=self.directory) != self:
            raise PermissionError('The installed worker declaration or executable changed')
        return self

    def receipt(self):
        parent = trusted_directory(self.directory)
        try:
            text = root_file(self.declaration.identifier+'.json',dir_fd=parent)
        finally:
            os.close(parent)
        if (hashlib.sha256(text.encode('utf-8')).hexdigest() != self.digest
                or Declaration.parse(text) != self.declaration):
            raise PermissionError('The package declaration changed before capture')
        self.verify()
        return {'text':text,'sha256':self.digest,'exe':list(self.executable),
                'recover_exe':list(self.recovery_executable)}

    @classmethod
    def from_receipt(cls, value):
        # Used to observe/stop the already captured old unit. A new serving
        # or recovery launch must still verify the current installed package.
        return cls(Declaration.parse(value['text']), value['sha256'], tuple(value['exe']),
                   tuple(value['recover_exe']), DIRECTORY)
