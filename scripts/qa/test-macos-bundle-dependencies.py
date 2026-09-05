#!/usr/bin/env python3
"""Check Mach-O dependency classification without requiring a macOS host or game data."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


source = Path(__file__).resolve().parents[1] / "build/macos/bundle_macos_echelon.py"
spec = importlib.util.spec_from_file_location("echelon_macos_bundle", source)
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class DependencyTests(unittest.TestCase):
    def read_dependencies(self, commands, libraries):
        def otool(*args):
            self.assertEqual(args[0], "otool")
            self.assertEqual(args[2], "/build/library")
            return {"-l": commands, "-L": libraries}[args[1]]

        with patch.object(bundle, "run", side_effect=otool):
            return bundle.dependencies(Path("/build/library"))

    def test_dylib_identity_is_not_a_dependency(self):
        commands = """Load command 0
          cmd LC_ID_DYLIB
      cmdsize 56
         name @rpath/libSDL3.0.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLIB
      cmdsize 56
         name /usr/lib/libSystem.B.dylib (offset 24)
"""
        libraries = """/build/library:
    @rpath/libSDL3.0.dylib (compatibility version 0.0.0, current version 0.4.2)
    /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
    @rpath/libother.dylib (compatibility version 1.0.0, current version 1.0.0)
"""
        self.assertEqual(self.read_dependencies(commands, libraries),
                         ["/usr/lib/libSystem.B.dylib", "@rpath/libother.dylib"])

    def test_executables_and_modules_keep_every_dependency(self):
        commands = """Load command 0
          cmd LC_LOAD_DYLIB
      cmdsize 56
         name @rpath/libSDL3.0.dylib (offset 24)
"""
        libraries = "/build/library:\n    @rpath/libSDL3.0.dylib (compatibility version 0.0.0)\n"
        self.assertEqual(self.read_dependencies(commands, libraries), ["@rpath/libSDL3.0.dylib"])


if __name__ == "__main__":
    unittest.main()
