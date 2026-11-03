#!/usr/bin/env python3
"""Pruebas de caja negra; cada proceso tiene un plazo y se mata si no cierra.

Uso: python3 tests/test_integration.py [--binary ./torneo] [--no-injection]
No requiere paquetes de terceros. Las partidas aleatorias no tienen un límite
artificial: los casos de terminación usan un tablero ganador o SIGINT.
"""

import argparse
import ctypes
import os
from pathlib import Path
import re
import resource
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
SIZE_MAX = (1 << (8 * ctypes.sizeof(ctypes.c_size_t))) - 1


class TournamentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="komikan-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def config(self, content):
        target = self.directory / "config.txt"
        target.write_text(content, encoding="utf-8")
        return str(target)

    def execute(self, *arguments, env=None, timeout=15, memory_limit=None):
        def restrict_memory():
            resource.setrlimit(resource.RLIMIT_AS, (memory_limit, memory_limit))

        completed = subprocess.run(
            [BINARY, *map(str, arguments)], cwd=ROOT, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            timeout=timeout, check=False,
            preexec_fn=restrict_memory if memory_limit is not None else None,
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

    def interrupt(self, arguments, ready, delay=0.0, env=None, after_signal=None):
        log = self.directory / "process.log"
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(
                [BINARY, *map(str, arguments)], cwd=ROOT,
                stdout=stream, stderr=subprocess.STDOUT,
                env=env,
            )
            try:
                deadline = time.monotonic() + 15
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
                if after_signal is not None:
                    after_signal()
                code = process.wait(timeout=5)
                if Path("/proc").is_dir():
                    self.assertFalse(Path(f"/proc/{process.pid}/task").exists(),
                                     "Quedó el proceso o alguno de sus hilos")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
        return code, log.read_text(encoding="utf-8", errors="replace")

    def injected_environment(self):
        shim = ROOT / "build" / "libfailpthread.so"
        self.assertTrue(shim.is_file(), "Compilar build/libfailpthread.so antes de probar")
        environment = os.environ.copy()
        existing = environment.get("LD_PRELOAD", "")
        environment["LD_PRELOAD"] = str(shim) + (":" + existing if existing else "")
        return environment

    def test_01_invalid_arguments(self):
        cases = [(), ("1",), ("0", "1"), ("2", "0"), ("-2", "1"),
                 ("2", "-1"), ("abc", "1"), ("2x", "1"),
                 ("2", "1", "a", "b"), (str(SIZE_MAX + 1), "1"),
                 ("2", str(SIZE_MAX + 1)), (str(SIZE_MAX), "1"),
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
                        "seed=4294967296\n", "fail_player=3\n", "fail_mode=unknown\n"):
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
        for failure in range(1, 11):
            with self.subTest(pthread_create_call=failure):
                environment = self.injected_environment()
                environment["KOMIKAN_FAIL_CREATE_AT"] = str(failure)
                code, output = self.execute(2, 1, env=environment)
                self.assertEqual(code, 1, output)
        # Comprueba que N=1025 llega a inicialización: la antigua restricción
        # lo rechazaba como argumento (2). Se falla el hilo emparejador para no
        # depender de los límites reales de hilos de la máquina de prueba.
        environment = self.injected_environment()
        environment["KOMIKAN_FAIL_CREATE_AT"] = "2"
        code, output = self.execute(1025, 1, env=environment)
        self.assertEqual(code, 1, output)


    def test_10_many_clients_fast_finishes_and_table_reuse(self):
        # Los jueces pueden terminar mientras otros se crean o se recogen.
        # No se exige que el planificador produzca un orden de finalización.
        for clients, tables_limit in ((2, 1), (10, 3), (100, 4), (200, 8)):
            with self.subTest(clients=clients, tables=tables_limit):
                code, output = self.execute(clients, tables_limit, self.config(SIEGE))
                self.assertEqual(code, 0, output)
                pairs = self.assert_fifo(output)
                matches_expected = clients // 2
                self.assertEqual(len(pairs), matches_expected, output)
                self.assertEqual(len(RESULT.findall(output)), matches_expected, output)
                self.assertTrue(all(int(result[2]) == 2 for result in RESULT.findall(output)), output)
                tables = {pair[1] for pair in pairs}
                self.assertTrue(1 <= len(tables) <= tables_limit, output)
                self.assertTrue(all(pair[6] <= tables_limit for pair in pairs), output)
                if matches_expected > tables_limit:
                    self.assertLess(len(tables), len(pairs), output)
                created, arrived, matches, done, peak, limit, queued = self.summary(output)
                self.assertEqual((created, arrived, matches, done, limit, queued),
                                 (clients, clients, matches_expected, matches_expected,
                                  tables_limit, 0), output)
                self.assertTrue(1 <= peak <= tables_limit, output)

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

    def test_13_previous_table_cap_is_not_an_argument_limit(self):
        for limit in (65, SIZE_MAX):
            with self.subTest(tables=limit):
                code, output = self.execute(2, limit, self.config(SIEGE))
                self.assertEqual(code, 0, output)
                self.assertEqual(self.summary(output), (2, 2, 1, 1, 1, limit, 0))

    @unittest.skipIf(OPTIONS.no_injection, "Agotamiento de memoria excluido de sanitizadores")
    def test_14_allocation_failure_is_handled(self):
        # N es representable y no desborda los productos de sizeof, pero las
        # reservas exceden el espacio virtual permitido a este proceso hijo.
        code, output = self.execute(100_000_000, 1, memory_limit=128 * 1024 * 1024)
        self.assertEqual(code, 1, output)

    @unittest.skipIf(OPTIONS.no_injection, "Inyección desactivada explícitamente")
    def test_15_partial_mutex_initialization_failures(self):
        for failure in (1, 2, 3):
            with self.subTest(mutex_init_call=failure):
                environment = self.injected_environment()
                environment["KOMIKAN_FAIL_MUTEX_AT"] = str(failure)
                code, output = self.execute(2, 1, env=environment)
                self.assertEqual(code, 1, output)

    @unittest.skipIf(OPTIONS.no_injection, "Barreras LD_PRELOAD excluidas de sanitizadores")
    def test_16_sigint_during_initialization_with_deterministic_gates(self):
        gates = (("KOMIKAN_GATE_CREATE_AT", "1", "pthread_create", (2, 1)),
                 ("KOMIKAN_GATE_CREATE_AT", "2", "pthread_create", (2, 1)),
                 ("KOMIKAN_GATE_CREATE_AT", "3", "pthread_create", (2, 1)),
                 ("KOMIKAN_GATE_CREATE_AT", "6", "pthread_create", (2, 1)),
                 ("KOMIKAN_GATE_MUTEX_AT", "2", "pthread_mutex_init", (2, 1)),
                 ("KOMIKAN_GATE_MUTEX_AT", "4", "pthread_mutex_init", (6, 3)))
        for selector, call, marker, arguments in gates:
            with self.subTest(operation=marker, invocation=call):
                fifo = self.directory / f"gate-{selector}-{call}"
                os.mkfifo(fifo)
                descriptor = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
                try:
                    environment = self.injected_environment()
                    environment[selector] = call
                    environment["KOMIKAN_GATE_FIFO"] = str(fifo)
                    code, output = self.interrupt(
                        arguments, lambda text: f"PRUEBA gate={marker}" in text,
                        env=environment,
                        after_signal=lambda: os.write(descriptor, b"x"),
                    )
                finally:
                    os.close(descriptor)
                self.assertIn("SERVIDOR señales_bloqueadas", output)
                self.assertEqual(code, 130, output)

    def test_17_sigint_after_early_block_marker(self):
        for iteration in range(10):
            with self.subTest(iteration=iteration):
                code, output = self.interrupt(
                    (20, 2), lambda text: "SERVIDOR señales_bloqueadas" in text,
                )
                self.assertEqual(code, 130, output)

    def test_18_sigint_with_two_hundred_clients(self):
        code, output = self.interrupt(
            (200, 4),
            lambda text: len(PAIR.findall(text)) >= 4 and len(ARRIVAL.findall(text)) == 200,
        )
        self.assertEqual(code, 130, output)
        self.assert_fifo(output)
        created, arrived, matches, done, peak, limit, queued = self.summary(output)
        self.assertEqual((created, arrived, peak, limit, queued), (200, 200, 4, 4, 0), output)
        self.assertEqual(matches, done, output)

    def test_19_immediate_sigint_classifies_pre_initialization_window(self):
        outcomes = {"before_block": 0, "controlled": 0}
        for iteration in range(10):
            log = self.directory / f"immediate-{iteration}.log"
            with log.open("w", encoding="utf-8") as stream:
                process = subprocess.Popen(
                    [BINARY, "20", "2"], cwd=ROOT,
                    stdout=stream, stderr=subprocess.STDOUT,
                )
                try:
                    process.send_signal(signal.SIGINT)
                    code = process.wait(timeout=5)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=3)
            output = log.read_text(encoding="utf-8", errors="replace")
            self.assertIn(code, (-signal.SIGINT, 130), output)
            if "SERVIDOR señales_bloqueadas" in output:
                self.assertEqual(code, 130, output)
            outcomes["controlled" if code == 130 else "before_block"] += 1
        print("SIGINT inmediato: "
              f"previo al bloqueo={outcomes['before_block']}, "
              f"cierre controlado={outcomes['controlled']}")

    def test_20_unexpected_participant_and_piece_failures(self):
        modes = (("client_exit_locked", "fallo_cliente"),
                 ("client_exit_unlocked", "fallo_cliente"),
                 ("piece_exit_locked", "fallo_pieza"),
                 ("piece_exit_unlocked", "fallo_pieza"),
                 ("piece_cancel_wait", "fallo_pieza"))
        for mode, reason in modes:
            with self.subTest(failure_mode=mode):
                configuration = self.config(
                    f"fail_player=1\nfail_after_ms=50\nfail_mode={mode}\n"
                )
                code, output = self.execute(2, 1, configuration)
                self.assertEqual(code, 0, output)
                result = RESULT.findall(output)
                self.assertEqual(len(result), 1, output)
                self.assertEqual(result[0][3].strip(), reason, output)
                pair = self.assert_fifo(output)[0]
                self.assertEqual(int(result[0][2]), 2 if pair[2] == 1 else 1, output)
                self.assertEqual(self.summary(output)[2:4], (1, 1), output)

    def test_21_fifo_and_directory_configuration_are_rejected(self):
        fifo = self.directory / "config.fifo"
        os.mkfifo(fifo)
        for path in (fifo, self.directory):
            with self.subTest(config=path.name):
                code, output = self.execute(2, 1, path, timeout=3)
                self.assertIn(code, (1, 2), output)

    @unittest.skipIf(OPTIONS.no_injection, "Barreras LD_PRELOAD excluidas de sanitizadores")
    def test_22_sigint_during_configuration_loading(self):
        fifo = self.directory / "config-gate"
        os.mkfifo(fifo)
        descriptor = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
        try:
            environment = self.injected_environment()
            environment["KOMIKAN_GATE_FDOPEN_AT"] = "1"
            environment["KOMIKAN_GATE_FIFO"] = str(fifo)
            configuration = self.config(SIEGE)
            code, output = self.interrupt(
                (2, 1, configuration), lambda text: "PRUEBA gate=fdopen" in text,
                env=environment, after_signal=lambda: os.write(descriptor, b"x"),
            )
        finally:
            os.close(descriptor)
        self.assertIn("SERVIDOR señales_bloqueadas", output)
        self.assertEqual(code, 130, output)


if __name__ == "__main__":
    unittest.main(argv=[__file__, *UNITTEST_ARGS], verbosity=2)
