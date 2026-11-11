# SPDX-License-Identifier: Apache-2.0
"""The text-against-text check of the silkscreen cleaner. Needs no KiCad."""

from hwflow import silk


def test_texts_that_read_as_one_word_or_overlap_are_pairs():
    boxes = [
        ("TP6", 1, False, 0.0, 0.0, 3.0, 1.0),
        ("TP21", 1, False, 3.1, 0.0, 6.9, 1.0),       # 0.1 mm right of TP6 on the same line: one word
        ("TP26", 1, False, 7.5, 0.0, 11.3, 1.0),      # 0.6 mm further on: a word of its own
        ("BACK", 1, False, 0.0, 1.1, 3.4, 2.1),       # the next line, 0.1 mm below: lines may be close
        ("TITLE", 2, False, 0.0, 0.0, 9.0, 1.0),      # the other side of the board
        ("SW2", 1, True, 11.0, -1.0, 12.0, 2.0),      # turned, and across the end of TP26
        ("J8", 1, True, 20.0, 0.0, 21.0, 2.0),
        ("J9", 1, True, 20.0, 2.2, 21.0, 4.2),        # turned texts end to end, 0.2 mm apart
    ]
    assert silk.crowded_texts(boxes, 0.5) == [("TP6", "TP21"), ("TP26", "SW2"), ("J8", "J9")]
    assert silk.crowded_texts(boxes, 0.05) == [("TP26", "SW2")]


def test_a_text_beyond_the_outline_or_on_its_edge_is_found():
    boxes = [
        ("J1", 1, False, 18.5, 112.1, 20.9, 114.2),       # wholly below a 114 x 110 board (y down)
        ("U6", 1, True, 112.4, 30.0, 114.1, 32.2),        # across the right edge
        ("TP1", 1, False, 50.0, 50.0, 53.0, 51.0),
        ("SW1", 1, False, 46.3, 0.2, 49.6, 1.6),          # 0.2 mm from the top edge, 0.3 is asked
    ]
    assert silk.off_board(boxes, (0.0, 0.0, 114.0, 110.0), 0.3) == ["J1", "U6", "SW1"]
