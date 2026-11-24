#!/usr/bin/env python3
"""Reference model for Stage 6D mathematical invariants. NOT native kernel tests."""
from dataclasses import dataclass, field

@dataclass
class ClassState:
    fast: int = 0
    slow: int = 0
    quality: int = 0
    last_fast: float = 0.
    last_slow: float = 0.
    last_credit: float = 0.
    last_refault: float = 0.
    credits: int = 0

    def age(self, now, pressure):
        fast, slow = ((2., 16.), (1., 8.), (.5, 4.))[pressure]
        if not self.last_fast:
            self.last_fast = now
        elif now >= self.last_fast + fast:
            elapsed = int((now - self.last_fast) / fast)
            self.fast = max(0, self.fast - elapsed)
            self.last_fast += elapsed * fast
        if not self.last_slow:
            self.last_slow = now
        elif now >= self.last_slow + slow:
            elapsed = int((now - self.last_slow) / slow)
            self.slow = max(0, self.slow - elapsed)
            self.quality = max(0, self.quality - elapsed)
            self.last_slow += elapsed * slow

    def refault(self, now, pressure):
        self.age(now, pressure)
        self.last_refault = now
        if self.last_credit and now < self.last_credit + .25:
            return False
        self.last_credit = now
        self.fast = min(15, self.fast + 3)
        self.slow = min(15, self.slow + 1)
        self.credits += 1
        return True

    def rotate(self, now, pressure, scanned, rotated):
        if scanned >= 16 and rotated >= (scanned + 1) // 2:
            self.age(now, pressure)
            self.fast = min(15, self.fast + 1)

    def reclaim(self, now, pressure, scanned, reclaimed):
        if scanned < 16:
            return
        self.age(now, pressure)
        if reclaimed >= scanned - scanned // 4:
            self.fast = max(0, self.fast - 1)
            self.quality = min(8, self.quality + 2)
        elif reclaimed <= scanned // 4:
            self.quality = max(0, self.quality - 1)

@dataclass
class Context:
    anon: ClassState = field(default_factory=ClassState)
    file: ClassState = field(default_factory=ClassState)

    def percentage(self, now, pressure, cap=20):
        states = (self.anon, self.file)
        for state in states:
            state.age(now, pressure)
        result = []
        for i, state in enumerate(states):
            other = states[1 - i]
            recent = bool(state.last_refault and now <= state.last_refault + 2)
            competing = bool(other.last_refault and now <= other.last_refault + 2)
            value = state.fast + state.slow
            if recent:
                value += 2
            elif competing and other.fast > state.fast + 3:
                value -= 3
            value -= state.quality
            result.append(max(0, min(cap, value)))
        return result

def scan_target(original, pct, *, mode, b_protection=True, urgent=False,
                low_swap=False, forced_retry=False, memcg=False):
    if not mode or not b_protection or urgent or low_swap or forced_retry or memcg:
        pct = 0
    return original - original * pct // 100

def run():
    for pressure in range(3):
        c = Context()
        for i in range(50):
            c.file.refault(1. + i * .01, pressure)
        assert c.file.credits <= 3, c.file.credits
        assert c.file.fast <= 15 and c.file.slow <= 15
        c.anon.refault(3., pressure)
        assert c.percentage(3., pressure)[0] > 0
        c.file.reclaim(3., pressure, 32, 32)
        assert c.file.quality == 2
        c.anon.rotate(3., pressure, 16, 9)
        assert 0 <= c.anon.fast <= 15
        c.percentage(300., pressure)
        assert c.percentage(300., pressure) == [0, 0]

    for size in range(512):
        for pct in range(26):
            out = scan_target(size, pct, mode=1)
            assert size - (size * 25 // 100) <= out <= size
            for bypass in ('urgent', 'low_swap', 'forced_retry', 'memcg'):
                assert scan_target(size, pct, mode=1, **{bypass: True}) == size
            assert scan_target(size, pct, mode=0) == size
            assert scan_target(size, pct, mode=1, b_protection=False) == size

    c = Context()
    c.file.refault(1., 0)
    assert c.percentage(1., 0)[1] > 0
    assert c.percentage(1., 0, cap=0) == [0, 0]
    assert max(c.percentage(1., 0, cap=25)) <= 25
    print('PASS: independent classes, rate-limited credits, adaptive decay,')
    print('      reclaim quality, transition hints, 25% cap and emergency bypasses')
    print('NOTE: host reference-model checks only; no native kernel build or boot test')

if __name__ == '__main__':
    run()
