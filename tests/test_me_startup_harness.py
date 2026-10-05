"""Synthetic fixtures: firmware is not needed; Unicorn checks are optional."""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import verify_me_startup as harness


class StartupHarnessTests(unittest.TestCase):
    def test_signed_displacement_and_wrapping(self):
        cases = [(0x8C82FFFF, 0x1000, ('read', 0xFFF, 2)),
                 (0xAC857FFF, 0x1000, ('write', 0x8FFF, 5)),
                 (0x8C828000, 0x1000, ('read', 0xFFFF9000, 2)),
                 (0xAC820004, 0xFFFFFFFC, ('write', 0, 2))]
        for word, base, expected in cases:
            with self.subTest(word=hex(word), base=hex(base)):
                self.assertEqual(harness.decode_word_access(word, lambda reg: base), expected)

    def test_zero_register_is_not_read_from_callback(self):
        def forbidden(reg):
            self.fail('architectural zero must not call the register callback')
        self.assertEqual(harness.decode_word_access(0x8C020004, forbidden), ('read', 4, 2))

    def test_non_memory_and_unaudited_memory(self):
        self.assertIsNone(harness.decode_word_access(0, lambda reg: 0))
        self.assertIsNone(harness.decode_word_access(0x03E00008, lambda reg: 0))
        for word in (0x80820000, 0xA4820000, 0xC4820000, 0xBC800000):
            with self.subTest(word=hex(word)), self.assertRaises(ValueError):
                harness.decode_word_access(word, lambda reg: 0)

    def test_trampolines_only_modify_import_slots(self):
        original = b'\xA5' * 0x3E60
        patched, tramp = harness.build_trampolines(original)
        allowed = {offset for start in harness.SERVICE_ARGS for offset in range(start, start + 8)}
        changed = {i for i, pair in enumerate(zip(original, patched)) if pair[0] != pair[1]}
        self.assertTrue(changed)
        self.assertTrue(changed <= allowed)
        for start, end in harness.RANGES:
            self.assertEqual(patched[start:end], original[start:end])
        for i, start in enumerate(harness.SERVICE_ARGS):
            jump, delay = struct.unpack_from('<II', patched, start)
            self.assertEqual(jump, 0x08000000 | ((harness.TRAMP + i * 16) >> 2))
            self.assertEqual(delay, 0)
        self.assertEqual(len(tramp), 4096)

    def test_incomplete_code_export_rejected(self):
        with self.assertRaises(ValueError):
            harness.build_trampolines(b'\0' * 8)

    def test_vectors_reproduce_and_cover_pending(self):
        vectors = harness.make_vectors(660, 7)
        self.assertEqual(vectors, harness.make_vectors(660, 7))
        self.assertEqual(len(vectors), 172 + 3 * 7)
        pending = [v for v in vectors if v['pending']]
        self.assertEqual(len(pending), 4)
        for v in pending:
            target = 1 if v['kind'] == 'boot' else int(v['argument'] != 0)
            self.assertNotIn(target, v['acks'])

    def test_physical_aliases(self):
        for address in (0x1FC007F0, 0x9FC007F0, 0xBFC007F0):
            self.assertEqual(harness.physical(address), 0x1FC007F0)
        self.assertEqual(harness.physical(0xD0000000), 0xD0000000)

    @unittest.skipUnless(importlib.util.find_spec('unicorn'), 'optional Unicorn not installed')
    def test_synthetic_service_call_preserves_delay_slot_stack(self):
        import unicorn as U
        import unicorn.mips_const as M
        if U.__version__ != '2.1.4':
            self.skipTest('the execution fixture pins Unicorn 2.1.4')
        code = bytearray(0x3E60)
        # Synthetic function: store S0 in a JAL delay slot, then restore frame.
        words = [0x27BDFFF0, 0xAFBF0004, 0x0C000F81, 0xAFB00000,
                 0x8FBF0004, 0x8FB00000, 0x03E00008, 0x27BD0010]
        struct.pack_into('<8I', code, 0x35D8, *words)
        baseline, tramp = harness.build_trampolines(code)
        machine = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
        machine.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
        for address, size in ((0, harness.MODULE_SIZE), (harness.STACK_PAGE, 4096),
                              (harness.STOP, 4096), (harness.TRAMP, 8192)):
            machine.mem_map(address, size)
        machine.mem_write(0, baseline)
        machine.mem_write(harness.TRAMP, tramp)
        events = []
        def service(uc, access, address, width, value, user):
            if address == harness.MAIL + 12:
                events.append(value)
                uc.mem_write(harness.MAIL + 16, struct.pack('<I', 0x12345678))
        machine.hook_add(U.UC_HOOK_MEM_WRITE, service,
                         begin=harness.MAIL, end=harness.MAIL + 15)
        machine.reg_write(M.UC_MIPS_REG_SP, harness.STACK)
        machine.reg_write(M.UC_MIPS_REG_S0, 0xABCDEF01)
        machine.reg_write(M.UC_MIPS_REG_RA, harness.ALIAS | harness.STOP)
        machine.emu_start(harness.ALIAS | 0x35D8, harness.ALIAS | harness.STOP, count=128)
        self.assertEqual(events, [0x3E04])
        self.assertEqual(machine.reg_read(M.UC_MIPS_REG_PC), harness.ALIAS | harness.STOP)
        self.assertEqual(machine.reg_read(M.UC_MIPS_REG_SP), harness.STACK)
        self.assertEqual(machine.reg_read(M.UC_MIPS_REG_S0), 0xABCDEF01)
        self.assertEqual(machine.reg_read(M.UC_MIPS_REG_V0), 0x12345678)


if __name__ == '__main__':
    unittest.main()
