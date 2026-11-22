#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render actual Karyo taskbar PE icons and group windows in a private session."""
import importlib.util
from pathlib import Path


def module(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    result=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def checks(env,output,baseline):
    assert not baseline, 'Actual shell rendering requires the positive compositor package'
    return module('desktop_icons','desktop-icons.py').checks(env,output,baseline,panel=True)


if __name__=='__main__':
    module('caption_harness','caption-buttons.py').main(checker=checks,description=__doc__,
        actor_path=Path(__file__),private_pointer=False,real_wine_application=True)
