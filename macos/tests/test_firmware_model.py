from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FirmwareModelTests(unittest.TestCase):
    def test_qspi_initialization_configuration_and_failures(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run firmware tests")
        with tempfile.TemporaryDirectory() as directory:
            stubs = Path(directory)
            for header in ("lvgl.h", "bsp/esp-bsp.h", "bsp/display.h",
                           "bsp/touch.h", "esp_lcd_touch.h", "esp_lvgl_port.h",
                           "esp_lvgl_port_touch.h", "esp_log.h"):
                path = stubs / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "display_port_stubs.hpp"\n')
            binary = str(stubs / "qspi_display_test")
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(stubs), "-I", str(ROOT / "firmware/tests"),
                "-I", str(ROOT / "firmware/main"),
                str(ROOT / "firmware/tests/qspi_display_test.cpp"),
                str(ROOT / "firmware/main/qspi_display.cpp"), "-o", binary,
            ], check=True, capture_output=True, text=True)
            subprocess.run([binary], check=True, capture_output=True, text=True)

    def test_actual_cpp_model_with_interleaved_raw_and_server_packets(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run firmware tests")
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "brush_model_test")
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "firmware/main"),
                str(ROOT / "firmware/tests/brush_model_test.cpp"),
                str(ROOT / "firmware/main/brush_model.cpp"), "-o", binary,
            ], check=True, capture_output=True, text=True)
            subprocess.run([binary], check=True, capture_output=True, text=True)
