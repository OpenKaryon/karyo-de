# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise guard seeding in an instrumented bootstrap, retaining other checks."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("clang"), "native Clang compiler control")
class BootstrapGuard(unittest.TestCase):
    def test_initializer_can_seed_the_guard_while_unrelated_frames_stay_protected(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0030-libsystem-initialize-stack-guard-before-protection.patch"
        lines = patch.read_text().splitlines()
        old = next(line[1:] for line in lines if line.startswith("-__attribute__"))
        new = next(line[1:] for line in lines if line.startswith("+__attribute__"))
        source = """extern void _Exit(int) __attribute__((noreturn));
volatile __UINTPTR_TYPE__ __stack_chk_guard = 42;
__attribute__((noreturn, no_stack_protector)) void __stack_chk_fail(void) { _Exit(42); }
__attribute__((noinline)) void seed(volatile char *p) { *p=17; __stack_chk_guard^=0x12345678; }
ATTRIBUTE
__attribute__((noinline)) static void libSystem_initializer(void) { volatile char callbacks[8]={0}; seed(callbacks); }
__attribute__((noinline)) int guarded(void) { volatile char callbacks[8]={0}; seed(callbacks); return callbacks[0]; }
int main(int argc, char **argv) { return argc==2 ? guarded() : (__stack_chk_guard==42); }
"""
        with tempfile.TemporaryDirectory(prefix="karyo-bootstrap-guard-") as directory:
            root = Path(directory)
            for label, attribute, expected in (("before",old,42),("after",new,0)):
                path=root/(label+".c");path.write_text(source.replace("ATTRIBUTE",attribute))
                binary=root/label
                subprocess.run(["clang","-O2","-fstack-protector-strong","-mstack-protector-guard=global",
                    str(path),"-o",str(binary)],check=True,capture_output=True)
                self.assertEqual(subprocess.run([str(binary)]).returncode,expected)
                if label=="after":
                    # The same binary still catches a guard change in an ordinary frame.
                    self.assertEqual(subprocess.run([str(binary),"guarded"]).returncode,42)
                ir=root/(label+".ll")
                subprocess.run(["clang","-target","x86_64-apple-darwin20","-O2","-fstack-protector-strong",
                    "-S","-emit-llvm",str(path),"-o",str(ir)],check=True,capture_output=True)
                text=ir.read_text()
                def attributes(function):
                    number=re.search(r"define[^\n]*@"+function+r"\([^\n]*#([0-9]+)",text).group(1)
                    return re.search(r"attributes #"+number+r" = \{([^}]+)\}",text).group(1)
                self.assertEqual("sspstrong" in attributes("libSystem_initializer"),label=="before")
                self.assertIn("sspstrong",attributes("guarded"))


if __name__ == "__main__":
    unittest.main()
