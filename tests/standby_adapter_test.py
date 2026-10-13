"""Guard build reproducibility and fail-closed dependency integration."""
import hashlib
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("standby_adapter", ROOT / "scripts/prepare_standby_adapter.py")
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)
COMPONENT = ROOT / "managed_components/espressif__esp_lvgl_adapter"


class StandbyAdapterChecks(unittest.TestCase):
    def test_managed_sources_unchanged_and_generation_repeatable(self):
        before = {name: (COMPONENT / name).read_bytes() for name in integration.HASHES}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            integration.prepare(COMPONENT, output)
            generated = {name: (output / name).read_bytes() for name in before}
            mtimes = {name: (output / name).stat().st_mtime_ns for name in before}
            integration.prepare(COMPONENT, output)
            for name in before:
                self.assertEqual(before[name], (COMPONENT / name).read_bytes())
                self.assertEqual(hashlib.sha256(before[name]).hexdigest(), integration.HASHES[name])
                self.assertNotEqual(before[name], generated[name])
                self.assertEqual(generated[name], (output / name).read_bytes())
                self.assertEqual(mtimes[name], (output / name).stat().st_mtime_ns)
            adapter = (output / integration.ADAPTER).read_text()
            self.assertIn("#define CHAMELEON_LV_WORKER_NOTIFICATION_INDEX 1U", adapter)
            self.assertIn("#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 2", adapter)
            self.assertEqual(adapter.count("ulTaskNotifyTakeIndexed(CHAMELEON_LV_WORKER_NOTIFICATION_INDEX"), 2)
            self.assertEqual(adapter.count("xTaskNotifyGiveIndexed("), 4)
            self.assertEqual(adapter.count("vTaskNotifyGiveIndexedFromISR("), 2)
            self.assertNotIn("ulTaskNotifyTake(", adapter)
            self.assertNotIn("xTaskNotifyGive(", adapter)
            self.assertNotIn("vTaskNotifyGiveFromISR(", adapter)
            display = (output / integration.DISPLAY).read_text()
            self.assertIn("#include \"chameleon_adapter.h\"", display)
            self.assertIn("chameleon_lv_adapter_notify();", display)
            self.assertNotIn("xTaskNotifyGive(", display)

    def test_touch_decoder_retains_baseline_and_drains_captured_reports(self):
        """Keep hardware decoding stable while integrating ordered capture."""
        baseline_script = subprocess.run(
            ["git", "show", "d1d97df:scripts/prepare_standby_adapter.py"],
            cwd=ROOT, capture_output=True, check=True,
        ).stdout
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            script = folder / "baseline_adapter.py"
            script.write_bytes(baseline_script)
            baseline_spec = importlib.util.spec_from_file_location("baseline_adapter", script)
            baseline = importlib.util.module_from_spec(baseline_spec)
            baseline_spec.loader.exec_module(baseline)
            baseline.prepare(COMPONENT, folder / "baseline")
            integration.prepare(COMPONENT, folder / "current")
            old = (folder / "baseline" / baseline.TOUCH).read_text()
            new = (folder / "current" / integration.TOUCH).read_text()
            signature = "static void lvgl_touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data)\n{"
            end = "static bool validate_touch_config("
            old_reader = old[old.index(signature):old.index(end, old.index(signature))]
            new_reader = new[new.index(signature):new.index(end, new.index(signature))]
            stamp = "            if (ret == ESP_OK) data->timestamp = chameleon_touch_sample_timestamp();\n"
            self.assertEqual(new_reader.count(stamp), 1)
            self.assertEqual(old_reader, new_reader.replace(stamp, ""))
            self.assertIn("reads < 64 && uxSemaphoreGetCount(tp->touch_sem) > 0", new)

    def test_changed_dependency_is_rejected_without_partial_output(self):
        for changed_name in integration.HASHES:
            with self.subTest(source=changed_name), tempfile.TemporaryDirectory() as directory:
                component, output = Path(directory) / "component", Path(directory) / "generated"
                for name in integration.HASHES:
                    target = component / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes((COMPONENT / name).read_bytes())
                target = component / changed_name
                target.write_bytes(target.read_bytes() + b"\n/* changed upstream */\n")
                with self.assertRaisesRegex(ValueError, "unsupported esp_lvgl_adapter"):
                    integration.prepare(component, output)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
