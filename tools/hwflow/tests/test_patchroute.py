# SPDX-License-Identifier: Apache-2.0
"""The maze router of the patch step, on made-up boards. Reading a KiCad board is not tested here."""

from hwflow import patchroute as pr

LAYERS = ["F.Cu", "In2.Cu", "B.Cu"]
F, ALL = frozenset(["F.Cu"]), frozenset(LAYERS + ["In1.Cu"])


def clearance(net):
    return 0.6 if net == "/WIDE" else 0.2


def grid_for(items, areas=(), net="/A", width=0.2, extra=(), window=(0.5, 0.5, 10.5, 6.5)):
    return pr.build_grid(list(items), list(areas), clearance, net, width, window, LAYERS, list(extra), {"GND"},
                         (12.0, 8.0))


def route(grid, a, b, la=F, lb=F):
    starts, goals = pr.nodes_of(grid, la, a), pr.nodes_of(grid, lb, b)
    for n in starts + goals:
        grid.blocked[grid.layers[n[0]]][n[2] * grid.w + n[1]] = 0
    return pr.search(grid, starts, goals)


def test_a_free_board_gives_a_straight_track():
    g = grid_for([])
    path = route(g, (1.0, 3.0), (9.0, 3.0))
    tracks = pr.to_tracks(g, path, "/A", 0.2)
    assert len(tracks) == 1 and tracks[0]["layer"] == "F.Cu"
    assert tracks[0]["points"] == [[1.0, 3.0], [9.0, 3.0]]


def test_a_wall_on_one_layer_is_passed_with_two_vias():
    wall = ("/B", F, ("seg", (5.0, 0.0), (5.0, 8.0), 0.3), "")
    g = grid_for([wall])
    path = route(g, (1.0, 3.0), (9.0, 3.0))
    tracks = pr.to_tracks(g, path, "/A", 0.2)
    # up beside the wall, across on another layer, down at the far end: the end is a bare point here
    assert tracks[0]["layer"] == "F.Cu" and {t["layer"] for t in tracks[1:]} <= {"In2.Cu", "B.Cu", "F.Cu"}
    assert sum(1 for t in tracks if "via" in t) == 2
    # the vias keep the clearance of the wall plus the via's own radius: 0.15 + 0.2 + 0.3
    for t in tracks:
        if "via" in t:
            assert abs(t["points"][-1]["via"][0] - 5.0) >= 0.65


def test_a_wall_on_every_layer_cannot_be_passed():
    wall = ("/B", ALL, ("seg", (5.0, 0.0), (5.0, 8.0), 0.3), "")
    g = grid_for([wall])
    assert route(g, (1.0, 3.0), (9.0, 3.0)) is None
    cells, box = pr.reach_of(g, pr.nodes_of(g, F, (1.0, 3.0)))
    assert cells > 100 and box[2] < 5.0            # the flood stays on its side of the wall


def test_the_gap_a_track_needs_is_width_and_two_clearances():
    def gap(g_mm):
        lower = ("/B", ALL, ("rect", (4.0, 0.0, 6.0, 3.0 - g_mm / 2)), "")
        upper = ("/B", ALL, ("rect", (4.0, 3.0 + g_mm / 2, 6.0, 8.0)), "")
        return route(grid_for([lower, upper]), (1.0, 3.0), (9.0, 3.0))
    assert gap(0.5) is None                        # 0.2 + 2 x 0.2 = 0.6 at least, plus the grid's margin
    assert gap(0.8) is not None


def test_a_net_that_asks_for_more_room_gets_it():
    post = ("/WIDE", ALL, ("circle", (5.0, 3.0), 0.3), "")
    path = route(grid_for([post]), (1.0, 3.0), (9.0, 3.0))
    pts = [pr.Grid.point(grid_for([post]), n[1], n[2]) for n in path]
    assert min(((x - 5.0) ** 2 + (y - 3.0) ** 2) ** 0.5 for x, y in pts) >= 0.3 + 0.6 + 0.1
    far = [("/GUARDED", ["*"], [], 1.0)]
    guarded = ("/GUARDED", ALL, ("circle", (5.0, 3.0), 0.3), "")
    g = grid_for([guarded], extra=far)
    path = route(g, (1.0, 3.0), (9.0, 3.0))
    assert min(((g.point(n[1], n[2])[0] - 5.0) ** 2 + (g.point(n[1], n[2])[1] - 3.0) ** 2) ** 0.5 for n in path) >= 1.4


def test_a_keepout_and_a_foreign_zone_block_and_a_pour_does_not():
    square = [(4.0, 0.0), (6.0, 0.0), (6.0, 8.0), (4.0, 8.0)]
    assert route(grid_for([], areas=[(ALL, square, True, True, "")]), (1.0, 3.0), (9.0, 3.0)) is None
    assert route(grid_for([], areas=[(ALL, square, True, True, "/ISLAND")]), (1.0, 3.0), (9.0, 3.0)) is None
    assert route(grid_for([], areas=[(ALL, square, True, True, "GND")]), (1.0, 3.0), (9.0, 3.0)) is not None
    # a keep-out on one layer only: over it on another
    path = route(grid_for([], areas=[(F, square, True, False, "")]), (1.0, 3.0), (9.0, 3.0))
    assert path is not None and {n[0] for n in path} != {0}


def test_no_via_in_a_surface_mount_pad_of_the_net_itself():
    wall = ("/B", F, ("seg", (5.0, 0.0), (5.0, 8.0), 0.3), "")
    pad = ("/A", F, ("rect", (8.5, 2.6, 9.5, 3.4)), "")
    g = grid_for([wall, pad])
    tracks = pr.to_tracks(g, route(g, (1.0, 3.0), (9.0, 3.0)), "/A", 0.2)
    last_via = [t for t in tracks if "via" in t][-1]["points"][-1]["via"]
    assert not (8.5 - 0.2 <= last_via[0] <= 9.5 + 0.2 and 2.6 - 0.2 <= last_via[1] <= 3.4 + 0.2)
    assert tracks[-1]["layer"] == "F.Cu" and tracks[-1]["points"][-1] == [9.0, 3.0]


def test_the_patch_file_names_its_session():
    text = pr.toml([{"layer": "F.Cu", "net": "/A", "width": 0.2, "stay": True, "via": [0.6, 0.3],
                     "points": [[1.0, 3.0], {"via": [2.0, 3.0]}]}], "abc123", ["/A: one"])
    assert '[patches]\nsession = "abc123"' in text
    assert "points = [[1, 3], {via = [2, 3]}]" in text and "via = [0.6, 0.3]" in text and "stay = true" in text
