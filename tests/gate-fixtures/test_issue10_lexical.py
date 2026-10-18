"""PR #38 F1/F2 regressions: source bytes and C11 translation-phase ordering."""
import unittest

import test_issue10_gate as fixtures


# A separate TestCase avoids running the original suite twice.
class Issue10LexicalRegressions(unittest.TestCase):
    setUp = fixtures.Issue10GateFixtures.setUp
    gate = fixtures.Issue10GateFixtures.gate

    def check_sources(self, sources, expected):
        sources = tuple(sources)
        for layer in fixtures.LAYERS:
            path = self.root / 'components' / layer / 'src/fixture.c'
            for source in sources:
                with self.subTest(layer=layer, source=source):
                    path.write_bytes(source.encode('utf-8'))
                    try:
                        self.gate(expected)
                    finally:
                        path.write_bytes(b'#include <stdint.h>\n')

    def test_bom_forbidden(self):
        self.check_sources(('\ufeff#include <esp_wifi.h>\n',
                            '\ufeff#include "esp_wifi.h"\n'), 1)

    def test_bom_legal(self):
        self.check_sources(('\ufeff#include <stdint.h>\n',
                            '\ufeff#include "stdint.h"\n'), 0)

    def trigraph_sources(self, header):
        for token in (f'<{header}>', f'"{header}"'):
            for directive in ('??=include ', '#inc??/\nlude ',
                              '#inc??/\r\nlude ', '??=inc??/\nlude ',
                              '#/**/inc??/\nlude /* explanation */ '):
                yield directive + token + '\n'

    def test_trigraph_forbidden(self):
        self.check_sources(self.trigraph_sources('esp_wifi.h'), 1)

    def test_trigraph_legal(self):
        self.check_sources(self.trigraph_sources('stdint.h'), 0)

    def test_trigraph_comment_splice_controls(self):
        self.check_sources((
            '// ??=include <esp_wifi.h>\n#include <stdint.h>\n',
            '/??/\n* #include <esp_wifi.h> */\n#include <stdint.h>\n',
            '// comment ??/\n??=include <esp_wifi.h>\n#include <stdint.h>\n',
        ), 0)

    def test_trigraph_splice_does_not_hide_following_directive(self):
        self.check_sources((
            '// comment ??/\ncontinued\n??=include <esp_wifi.h>\n',
            '#include <esp_??/\nwifi.h>\n',
            '#include "esp_??/\nwifi.h"\n',
        ), 1)

    def test_non_trigraph_mode_still_checked(self):
        # With trigraphs inactive, this ordinary include is outside the comment.
        self.check_sources(('// comment ??/\n#include <esp_wifi.h>\n',), 1)

    def test_trigraph_malformed_header_fails_closed(self):
        self.check_sources(('??=include <stdint.h\n',), 1)
