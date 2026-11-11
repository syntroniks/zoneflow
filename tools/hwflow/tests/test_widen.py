# SPDX-License-Identifier: Apache-2.0
"""Which widened tracks go back to their old width. The DRC run itself needs KiCad and is not tested here."""

from hwflow import widen


def finding(kind, *uuids):
    return {"type": kind, "items": [{"uuid": u} for u in uuids]}


def test_a_track_named_in_a_clearance_finding_goes_back():
    found = [finding("clearance", "a", "pad-1"), finding("hole_clearance", "b", "via-9")]
    assert widen.revert(found, {"a", "b", "c"}) == {"a", "b"}


def test_two_widened_tracks_too_close_to_each_other_both_go_back():
    assert widen.revert([finding("clearance", "a", "b")], {"a", "b"}) == {"a", "b"}


def test_findings_a_wider_track_cannot_cause_are_not_its_fault():
    # Before the zones are filled every track end at a pour is "dangling" and half the board "unconnected".
    found = [finding("track_dangling", "a"), finding("via_dangling", "v"), finding("silk_overlap", "a", "t")]
    assert widen.revert(found, {"a"}) == set()


def test_a_finding_between_other_items_changes_nothing():
    assert widen.revert([finding("clearance", "x", "y")], {"a"}) == set()
