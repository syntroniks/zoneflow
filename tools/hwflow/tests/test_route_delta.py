# SPDX-License-Identifier: Apache-2.0
"""Reusing a stored routing after parts were taken off the board: the DSN delta and the session's nets."""

import json

import pytest

from hwflow import route, session

ROUTED = """(pcb "/somewhere/board.dsn"
  (placement
    (component "Resistor_SMD:R_0603_1608Metric"
      (place R66 173200 -56400 front 90 (PN 3.3k))
      (place R71 171700 -56400 front 90 (PN 10k))
    )
    (component "Capacitor_SMD:C_0603_1608Metric"
      (place C41 170200 -56400 front 90 (PN 100n))
    )
  )
  (network
    (net GND
      (pins SW3-S2 C41-2)
    )
    (net /ENC_SW
      (pins U4-31 R71-2 C41-1)
    )
    (net /Display/ENC_SW_SW
      (pins SW3-S1 R71-1 R66-2)
    )
  )
  (wiring
    (wire (path F.Cu 300  170200 -55625  169325 -55625)(net GND)(type fix))
    (via "Via[0-3]_600:300_um"  169325 -55625 (net GND)(type fix))
  )
)
"""
NOW = """(pcb "/elsewhere/board.dsn"
  (placement
    (component "Resistor_SMD:R_0603_1608Metric"
      (place R66 173200 -56400 front 90 (PN 3.3k))
    )
    (component "Capacitor_SMD:C_0603_1608Metric"
    )
  )
  (network
    (net GND
      (pins SW3-S2)
    )
    (net /ENC_SW
      (pins U4-31 SW3-S1 R66-2)
    )
  )
  (wiring
  )
)
"""


def body(text: str) -> str:
    return text.split("\n", 1)[1]


def test_taking_the_delta_back_gives_the_routed_file_line_for_line():
    delta = route.make_delta(body(ROUTED), body(NOW))
    assert route.undo_delta(body(NOW), delta) == body(ROUTED)
    assert route.undo_delta(body(NOW), route.DELTA_HEAD.format(stamp="x", digest="y") + delta) == body(ROUTED)
    gone, new = route.delta_counts(delta)
    assert (gone, new) == (9, 2)
    assert "-      (place R71 171700 -56400 front 90 (PN 10k))" in delta       # a reviewer reads what went
    assert "+      (pins U4-31 SW3-S1 R66-2)" in delta                          # and what the nets are now


def test_no_difference_is_an_empty_delta():
    assert route.make_delta(body(ROUTED), body(ROUTED)).strip() == ""
    assert route.undo_delta(body(ROUTED), "") == body(ROUTED)


@pytest.mark.parametrize("change", [
    lambda t: t.replace("173200 -56400", "173300 -56400"),          # a part that stayed has moved
    lambda t: t.replace("(pins SW3-S2)", "(pins SW3-S2 U4-1)"),     # a pin joined a net the delta speaks of
])
def test_a_delta_does_not_cover_another_change(change):
    delta = route.make_delta(body(ROUTED), body(NOW))
    changed = change(body(NOW))
    assert changed != body(NOW)
    try:
        back = route.undo_delta(changed, delta)
    except ValueError:
        return                                  # the lines the delta names are not there any more
    assert route.text_hash(back, "s") != route.text_hash(body(ROUTED), "s")


def test_a_delta_that_is_cut_short_or_not_a_delta_is_refused():
    delta = route.make_delta(body(ROUTED), body(NOW))
    with pytest.raises(ValueError):
        route.undo_delta(body(NOW), delta.rsplit("\n", 3)[0])
    with pytest.raises(ValueError):
        route.undo_delta(body(NOW), "this is not a delta\n")


def project(tmp_path, dsn_text: str):
    (tmp_path / "build").mkdir(exist_ok=True)
    (tmp_path / "route").mkdir(exist_ok=True)
    (tmp_path / "build" / "model.json").write_text(json.dumps({"name": "b"}))
    (tmp_path / "build" / "b.dsn").write_text(dsn_text)
    return tmp_path


def test_the_route_step_reuses_the_session_with_a_delta_and_refuses_it_without(tmp_path, capsys):
    routed = tmp_path / "routed.dsn"
    routed.write_text(ROUTED)
    p = project(tmp_path, NOW)
    (p / "route" / "b.ses").write_text("(session)")
    (p / "route" / "b.dsn.sha256").write_text(route.dsn_hash(routed) + "\n")
    args = [str(p), "--cache", str(tmp_path / "cache"), "--stored-only"]

    assert route.main(args) == 1                                   # another board, and nothing says how
    assert "was not made from the board" in capsys.readouterr().err

    assert route.main([*args, "--write-delta", str(routed)]) == 0
    assert (p / "route" / "b.dsn.delta").read_text().startswith("# What the Specctra file")
    assert route.main(args) == 0
    assert "differs from the routed one by" in capsys.readouterr().out

    # One more change to the board after the delta was written: the session is refused again.
    (p / "build" / "b.dsn").write_text(NOW.replace("173200", "173300"))
    assert route.main(args) == 1
    err = capsys.readouterr().err
    assert "b.dsn.delta" in err and "was not made from the board" in err


def test_a_delta_is_written_only_from_the_routed_file(tmp_path, capsys):
    routed = tmp_path / "routed.dsn"
    routed.write_text(ROUTED)
    p = project(tmp_path, NOW)
    (p / "route" / "b.ses").write_text("(session)")
    (p / "route" / "b.dsn.sha256").write_text(route.dsn_hash(routed) + "\n")
    other = tmp_path / "other.dsn"
    other.write_text(ROUTED.replace("171700", "171800"))
    assert route.main([str(p), "--cache", str(tmp_path / "cache"), "--write-delta", str(other)]) == 1
    assert "is not the file the stored session was routed from" in capsys.readouterr().err
    assert not (p / "route" / "b.dsn.delta").exists()
    # A routed file that differs from the stamped one only in the value text of a part is the same routing.
    (p / "route" / "b.dsn.novalues.sha256").write_text(route.dsn_hash(routed, values=False) + "\n")
    revalued = tmp_path / "revalued.dsn"
    revalued.write_text(ROUTED.replace("(PN 3.3k)", "(PN 3k3)"))
    (p / "build" / "b.dsn").write_text(NOW.replace("(PN 3.3k)", "(PN 3k3)"))
    assert route.main([str(p), "--cache", str(tmp_path / "cache"), "--write-delta", str(revalued)]) == 0
    assert route.main([str(p), "--cache", str(tmp_path / "cache"), "--stored-only"]) == 0


SES = """(session "b"
  (placement
    (resolution um 10)
    (component "Resistor_SMD:R_0603_1608Metric"
      (place R66 1732000 -564000 front 90)
      (place R71 1717000 -564000 front 90)
    )
  )
  (routes
    (network_out
      (net "/ENC_SW"
        (wire
          (path F.Cu 2000
            1 2
            3 4
          )
        )
      )
      (net "/Display/ENC_SW_SW"
        (wire
          (path F.Cu 2000
            5 6
            7 8
          )
        )
      )
      (net V5
        (wire
          (path F.Cu 4000
            9 9
            8 8
          )
        )
      )
    )
  )
)
"""
BOARD_NETS = {"/ENC_SW", "V5", "GND"}
BOARD_REFS = {"R66", "SW3", "U4"}


def test_the_copper_of_a_lost_net_is_imported_under_the_name_the_table_gives():
    out, what = session.translate(SES, BOARD_NETS, BOARD_REFS, {"/Display/ENC_SW_SW": "/ENC_SW"})
    assert session.nets(out) == ["/ENC_SW", "/ENC_SW", "V5"]
    assert out.count("(wire") == 3 and "5 6" in out                 # no copper is lost
    assert "(place R71" not in out and "(place R66 1732000 -564000 front 90)" in out
    assert what == {"renamed": {"/Display/ENC_SW_SW": "/ENC_SW"}, "placements_left_out": ["R71"]}
    assert out.count("(") == out.count(")")


def test_a_session_of_the_board_as_it_is_passes_unchanged():
    nets = BOARD_NETS | {"/Display/ENC_SW_SW"}
    out, what = session.translate(SES, nets, BOARD_REFS | {"R71"}, {})
    assert out == SES and what == {"renamed": {}, "placements_left_out": []}


@pytest.mark.parametrize("renamed, message", [
    ({}, "copper of nets that are not on the board: /Display/ENC_SW_SW"),          # nobody said where it belongs
    ({"/Display/ENC_SW_SW": "/NOWHERE"}, "not on the board: /NOWHERE"),
    ({"/Display/ENC_SW_SW": "/ENC_SW", "V5": "/ENC_SW"}, "still on the board: V5"),
    ({"/Display/ENC_SW_SW": "/ENC_SW", "/GONE": "/ENC_SW"}, "no copper of: /GONE"),
])
def test_a_guess_is_never_made_about_a_net(renamed, message):
    with pytest.raises(session.SessionError, match=message):
        session.translate(SES, BOARD_NETS, BOARD_REFS, renamed)
