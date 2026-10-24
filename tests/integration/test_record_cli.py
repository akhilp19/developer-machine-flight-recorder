"""End-to-end regressions using isolated config/data directories."""
import argparse
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import tempfile
import time
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--cli', required=True)
options, remaining = parser.parse_known_args()
CLI = str(Path(options.cli).resolve())

@unittest.skipUnless(os.name == 'posix', 'POSIX recording only')
class Recording(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dmfr-regression-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'config.json'
        self.data = self.root / 'data'
        self.config.write_text(json.dumps({'data_dir': str(self.data)}))

    def run_cli(self, *args, **kwargs):
        return subprocess.run([CLI, '-c', str(self.config), *args], cwd=self.root,
                              capture_output=True, text=True, timeout=10, **kwargs)

    def events(self):
        with sqlite3.connect(self.data / 'events.db') as db:
            return [json.loads(row[0]) for row in db.execute('select payload from events')]

    def test_argument_secrets_never_persist(self):
        result = self.run_cli('record', '--', '/bin/echo', '--password=DUMMY1', '--token', 'DUMMY2')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.events()[0]['args']
        self.assertEqual(args, ['--password=<REDACTED>', '--token', '<REDACTED>'])

    def test_initialization_failure_runs_command_once(self):
        for config in ({'data_dir': str(self.root / 'not-a-directory')},
                       {'data_dir': str(self.data), 'scrubber': {'secret_regexes': ['[']}}):
            (self.root / 'not-a-directory').write_text('file')
            self.config.write_text(json.dumps(config))
            result = self.run_cli('record', '--', '/bin/sh', '-c', 'echo ran >> marker; exit 7')
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertIn('without telemetry', result.stderr)
        self.assertEqual((self.root / 'marker').read_text(), 'ran\nran\n')

    def test_terminal_interrupt_is_recorded(self):
        proc = subprocess.Popen([CLI, '-c', str(self.config), 'record', '--', '/bin/sh', '-c',
                                 'touch started; sleep 15'], cwd=self.root,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            deadline = time.monotonic() + 8
            while not (self.root / 'started').exists() and time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                time.sleep(.02)
            self.assertTrue((self.root / 'started').exists())
            os.killpg(proc.pid, signal.SIGINT)
            self.assertEqual(proc.wait(timeout=3), 130)
            self.assertEqual(self.events()[0]['exit_code'], 130)
        finally:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()

    def test_hanging_probe_does_not_block_build(self):
        tools = self.root / 'tools'
        tools.mkdir()
        node = tools / 'node'
        node.write_text('#!/bin/sh\n/bin/sleep 30\n')
        node.chmod(0o700)
        env = dict(os.environ, PATH=str(tools) + ':' + os.environ.get('PATH', ''))
        started = time.monotonic()
        result = self.run_cli('record', '--', '/bin/sh', '-c', 'echo ran', env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'ran')
        self.assertLess(time.monotonic() - started, 5)
        with sqlite3.connect(self.data / 'events.db') as db:
            snapshot = json.loads(db.execute('select manifest from snapshots').fetchone()[0])
        self.assertIn({'tool': 'node', 'reason': 'timeout'}, snapshot['notes']['probe_warnings'])

    def test_recording_runs_retention_without_daemon(self):
        self.config.write_text(json.dumps({'data_dir': str(self.data), 'retention': {
            'enabled': True, 'max_events': 1, 'max_age_hours': 24, 'interval_minutes': 0}}))
        for _ in range(3):
            result = self.run_cli('record', '--', '/bin/sh', '-c', 'exit 0')
            self.assertEqual(result.returncode, 0, result.stderr)
        with sqlite3.connect(self.data / 'events.db') as db:
            self.assertEqual(db.execute('select count(*) from events').fetchone()[0], 1)
            self.assertEqual(db.execute('select count(*) from snapshots').fetchone()[0], 1)

    def test_same_basename_scripts_are_not_matching_commands(self):
        for folder, code in (('one', 0), ('two', 1)):
            directory = self.root / folder
            directory.mkdir()
            script = directory / 'build'
            script.write_text(f'#!/bin/sh\nexit {code}\n')
            script.chmod(0o700)
            result = self.run_cli('record', '--', str(script))
            self.assertEqual(result.returncode, code, result.stderr)
        report = self.run_cli('diagnose')
        self.assertEqual(json.loads(report.stdout)['status'], 'no_matching_success')

if __name__ == '__main__':
    unittest.main(argv=['test_record_cli', *remaining])
