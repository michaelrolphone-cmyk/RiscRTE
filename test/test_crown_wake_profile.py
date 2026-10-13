import configparser
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CrownWakeProfile(unittest.TestCase):
    def test_only_existing_observers_are_added(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read([ROOT / "platformio.ini", ROOT / "crown-build.ini",
                     ROOT / "crown-wake-profile.ini"])
        profile = config["env:crown-wake-profile"]
        self.assertEqual(profile["extends"], "env:crown-recovery-qualification")
        self.assertEqual(set(profile), {"extends", "build_flags"})
        self.assertEqual(profile["build_flags"].split(), [
            "${env:crown-recovery-qualification.build_flags}",
            "-DRISC_STAGE_LOGS=1", "-DRISC_PERFORMANCE_TRACE=1"])
        crown = config["env:crown-recovery-qualification"]
        self.assertEqual(crown["extends"], "env:esp32s3-16mb-appdata-iq-sharing")
        self.assertNotIn("RISC_STAGE_LOGS", crown["build_flags"])
        self.assertNotIn("RISC_PERFORMANCE_TRACE", crown["build_flags"])
        for section in config.sections():
            if section != "env:crown-wake-profile":
                self.assertNotIn("env:crown-wake-profile", config[section].get("extends", ""))


if __name__ == "__main__":
    unittest.main()
