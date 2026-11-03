#!/usr/bin/env python3
"""Pruebas de caja negra; cada proceso tiene un plazo y se mata si no cierra.

Uso: python3 tests/test_integration.py [--binary ./torneo] [--no-injection]
No requiere paquetes de terceros. Las partidas aleatorias no tienen un límite
artificial: los casos de terminación usan un tablero ganador o SIGINT.
"""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import unittest


PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("--binary", default="./torneo")
PARSER.add_argument("--no-injection", action="store_true")
OPTIONS, UNITTEST_ARGS = PARSER.parse_known_args()
BINARY = str(Path(OPTIONS.binary).resolve())
ROOT = Path(__file__).resolve().parent.parent
PAIR = re.compile(
    r"PAREJA partida=(\d+) mesa=(\d+) puma=(\d+) perros=(\d+) "
    r"ordenes=(\d+),(\d+) activas=(\d+)"
)
ARRIVAL = re.compile(r"LLEGADA jugador=(\d+) orden=(\d+)")
RESULT = re.compile(
    r"RESULTADO partida=(\d+) mesa=(\d+) ganador=(-?\d+) motivo=([^\n]+)"
)
SUMMARY = re.compile(
    r"RESUMEN creados=(\d+) llegados=(\d+) partidas=(\d+) "
    r"completadas=(\d+) pico_mesas=(\d+) limite=(\d+) en_cola=(\d+)"
)
SIEGE = "puma=24\ndogs=19,23,14,22,0,1,2,3,4,5,6,7\n"


class TournamentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="komikan-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def config(self, content):
        target = self.directory / "config.txt"
        target.write_text(content, encoding="utf-8")
        return str(target)

    def execute(self, *arguments, env=None, timeout=8):
        completed = subprocess.run(
            [BINARY, *map(str, arguments)], cwd=ROOT, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            timeout=timeout, check=False,
        )
        return completed.returncode, completed.stdout

    def summary(self, output):
        found = SUMMARY.findall(output)
        self.assertEqual(len(found), 1, output)
        return tuple(map(int, found[0]))

    def assert_fifo(self, output):
        pairs = sorted((tuple(map(int, pair)) for pair in PAIR.findall(output)))
        arrivals = {int(order): int(player) for player, order in ARRIVAL.findall(output)}
        for number, pair in enumerate(pairs):
            _match, _table, puma, dogs, first, second, _active = pair
            self.assertEqual((first, second), (2 * number + 1, 2 * number + 2), output)
            self.assertEqual(arrivals[first], puma, output)
            self.assertEqual(arrivals[second], dogs, output)
        return pairs

    def interrupt(self, arguments, ready, delay=0.0):
        log = self.directory / "process.log"
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(
                [BINARY, *map(str, arguments)], cwd=ROOT,
                stdout=stream, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 8
                while True:
                    output = log.read_text(encoding="utf-8", errors="replace")
                    if ready(output):
                        break
                    if process.poll() is not None or time.monotonic() >= deadline:
                        self.fail("No se alcanzó el estado esperado:\n" + output)
                    time.sleep(0.01)
                if delay:
                    time.sleep(delay)
                process.send_signal(signal.SIGINT)
                code = process.wait(timeout=5)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
        return code, log.read_text(encoding="utf-8", errors="replace")

    def test_01_invalid_arguments(self):
        cases = [(), ("1",), ("0", "1"), ("2", "0"), ("-2", "1"),
                 ("2", "-1"), ("abc", "1"), ("2x", "1"),
                 ("2", "1", "a", "b"), ("1025", "1"), ("2", "65"),
                 ("99999999999999999999999999999999", "1")]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                code, output = self.execute(*arguments)
                self.assertIn(code, (1, 2), output)

    def test_02_invalid_configuration(self):
        code, output = self.execute(2, 1, self.directory / "missing.txt")
        self.assertIn(code, (1, 2), output)
        for content in ("unknown=1\n", "seed=1\nseed=2\n",
                        "min_delay_ms=49\n", "max_delay_ms=301\n",
                        "puma=13\n", "dogs=1,2\n", "puma=28\n",
                        "min_delay_ms=300\nmax_delay_ms=50\n",
                        "seed=4294967296\n", "fail_player=3\n"):
            with self.subTest(content=content):
                code, output = self.execute(2, 1, self.config(content))
                self.assertIn(code, (1, 2), output)

    def test_03_single_unpaired_client(self):
        code, output = self.execute(1, 1)
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r"SIN_RIVAL jugador=1")
        self.assertEqual(self.summary(output), (1, 1, 0, 0, 0, 1, 0))

    def test_04_one_finished_game(self):
        code, output = self.execute(2, 1, self.config(SIEGE))
        self.assertEqual(code, 0, output)
        self.assertEqual(len(self.assert_fifo(output)), 1, output)
        results = RESULT.findall(output)
        self.assertEqual(len(results), 1, output)
        self.assertEqual(int(results[0][2]), 2, output)
        self.assertEqual(self.summary(output), (2, 2, 1, 1, 1, 1, 0))

    def test_05_multiple_games_table_reuse_and_odd_client(self):
        code, output = self.execute(11, 2, self.config(SIEGE))
        self.assertEqual(code, 0, output)
        pairs = self.assert_fifo(output)
        self.assertEqual(len(pairs), 5, output)
        self.assertLessEqual(len({pair[1] for pair in pairs}), 2, output)
        self.assertTrue(all(pair[6] <= 2 for pair in pairs), output)
        self.assertEqual(len(RESULT.findall(output)), 5, output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, matches, done, limit, queued),
                         (11, 11, 5, 5, 2, 0), output)
        self.assertTrue(1 <= peak <= 2, output)
        self.assertEqual(len(re.findall(r"SIN_RIVAL jugador=\d+", output)), 1, output)

    def test_06_concurrent_games_waiting_pairs_and_sigint(self):
        code, output = self.interrupt(
            (12, 2),
            lambda text: len(PAIR.findall(text)) >= 2 and len(ARRIVAL.findall(text)) == 12,
            delay=0.5,
        )
        self.assertEqual(code, 130, output)
        pairs = self.assert_fifo(output)
        self.assertGreaterEqual(len(pairs), 2, output)
        self.assertTrue(all(pair[6] <= 2 for pair in pairs), output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, peak, limit, queued), (12, 12, 2, 2, 0), output)
        self.assertEqual(matches, done, output)
        self.assertRegex(output.lower(), r"movimiento|jugada")

    def test_07_sigint_during_arrivals_before_pairing(self):
        configuration = self.config("arrival_delay_ms=500\n")
        code, output = self.interrupt(
            (8, 1, configuration), lambda text: len(ARRIVAL.findall(text)) == 1,
        )
        self.assertEqual(code, 130, output)
        self.assertFalse(PAIR.findall(output), output)
        summary = self.summary(output)
        self.assertEqual(summary[2:5], (0, 0, 0), output)
        self.assertEqual(summary[-1], 0, output)

    def test_08_abandonment(self):
        code, output = self.execute(2, 1, self.config("fail_player=1\nfail_after_ms=50\n"))
        self.assertEqual(code, 0, output)
        result = RESULT.findall(output)
        self.assertEqual(len(result), 1, output)
        self.assertIn("abandono", result[0][3].lower(), output)
        pair = self.assert_fifo(output)[0]
        expected = 2 if pair[2] == 1 else 1
        self.assertEqual(int(result[0][2]), expected, output)
        self.assertEqual(self.summary(output)[2:4], (1, 1), output)

    @unittest.skipIf(OPTIONS.no_injection, "Inyección desactivada explícitamente")
    def test_09_thread_creation_failures(self):
        shim = ROOT / "build" / "libfailpthread.so"
        self.assertTrue(shim.is_file(), "Compilar build/libfailpthread.so antes de probar")
        for failure in range(1, 11):
            with self.subTest(pthread_create_call=failure):
                environment = os.environ.copy()
                existing = environment.get("LD_PRELOAD", "")
                environment["LD_PRELOAD"] = str(shim) + (":" + existing if existing else "")
                environment["KOMIKAN_FAIL_CREATE_AT"] = str(failure)
                code, output = self.execute(2, 1, env=environment)
                self.assertEqual(code, 1, output)

    def test_10_many_clients_fast_finishes_and_table_reuse(self):
        # Los jueces pueden terminar mientras otros se crean o se recogen.
        # No se exige que el planificador produzca un orden de finalización.
        code, output = self.execute(100, 4, self.config(SIEGE))
        self.assertEqual(code, 0, output)
        pairs = self.assert_fifo(output)
        self.assertEqual(len(pairs), 50, output)
        self.assertEqual(len(RESULT.findall(output)), 50, output)
        self.assertTrue(all(int(result[2]) == 2 for result in RESULT.findall(output)), output)
        tables = {pair[1] for pair in pairs}
        self.assertTrue(1 <= len(tables) <= 4, output)
        self.assertTrue(all(pair[6] <= 4 for pair in pairs), output)
        self.assertLess(len(tables), len(pairs), output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, matches, done, limit, queued),
                         (100, 100, 50, 50, 4, 0), output)
        self.assertTrue(1 <= peak <= 4, output)

    def test_11_sigint_one_busy_table_and_waiting_clients(self):
        code, output = self.interrupt(
            (10, 1),
            lambda text: len(PAIR.findall(text)) >= 1 and len(ARRIVAL.findall(text)) == 10,
            delay=0.15,
        )
        self.assertEqual(code, 130, output)
        pairs = self.assert_fifo(output)
        self.assertGreaterEqual(len(pairs), 1, output)
        self.assertTrue(all(pair[6] == 1 for pair in pairs), output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, peak, limit, queued), (10, 10, 1, 1, 0), output)
        self.assertEqual(matches, done, output)

    def test_12_stress_four_concurrent_tables_and_sigint(self):
        code, output = self.interrupt(
            (40, 4),
            lambda text: len(PAIR.findall(text)) >= 4 and len(ARRIVAL.findall(text)) == 40,
            delay=0.15,
        )
        self.assertEqual(code, 130, output)
        pairs = self.assert_fifo(output)
        self.assertGreaterEqual(len(pairs), 4, output)
        self.assertTrue(all(pair[6] <= 4 for pair in pairs), output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, peak, limit, queued), (40, 40, 4, 4, 0), output)
        self.assertEqual(matches, done, output)


if __name__ == "__main__":
    unittest.main(argv=[__file__, *UNITTEST_ARGS], verbosity=2)
