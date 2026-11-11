// ZoneFlow test-fit kit: printable stand-ins for the circuit board, its
// plugs and the display panel, to check the enclosure by hand before any
// board is ordered.
//
// Included by zoneflow.scad, after board_params.scad, params.scad and
// testfit_params.scad. The stand-ins come from testfit_params.scad, which
// tools/gen_testfit.py writes from the board file and the mechanical
// contract. This file only decides how they are printed.
//
// Everything is modelled in board coordinates, in its assembled position,
// and turned into print orientation for export, like the enclosure parts.
//
// What is changed from the true shape so that every piece prints without
// support, and nothing else:
//   - The 2 mm of each terminal header beyond the board edge is on the plug
//     piece, not on the board. The plug piece pushes in from the wiring side
//     as a real plug does, and its prongs find pockets in the header block.
//   - The USB-C mouth, 1 mm beyond the edge, has a 45 degree underside.
//   - The space under the module's antenna end is filled down to the bed.
//   - C1 and K1, on the bottom side, are separate pieces with a rail that
//     fits a slot in the underside of the board.
//   - First-layer edges are chamfered by tf_cham.

// ---- Print allowances ---------------------------------------------------
tf_cham      = 0.4;    // 45 degree chamfer on every edge that lies on the bed
tf_mark      = 0.4;    // depth of engraved markings, two layers
tf_min_wall  = 0.8;    // nothing printed is thinner than this, the panel's cable apart
// Bottom-side pieces: a rail on the piece, a blind slot under the board.
tf_rail_w    = 2.4;
tf_rail_h    = 1.0;
tf_slot_d    = 1.1;    // leaves 0.5 mm of board over the slot
tf_rail_fit  = 0.15;   // per side
tf_rail_short = 4.0;   // the rail stops this far before the far end of the part
// Plug pieces: prongs into pockets in the face of each header block.
tf_hdr_wall  = 1.2;    // side walls and ribs of a header block
tf_pocket_max = 8.0;   // widest pocket, so that its roof is a short bridge
tf_pocket_z  = [1.6, 5.4];
tf_pocket_depth = 4.7;
tf_prong_len = 4.5;
tf_prong_fit = [0.15, 0.2];   // per side: across, and above and below
// Display panel stand-in.
tf_fpc_t     = 0.4;    // the cable is two layers thick, so that it can be folded
tf_fpc_root_len = 10;  // the wide part of the cable: "first 10 mm" in the contract
tf_fpc_insert = 3.3;   // length inside J7: the contract's fpc_reach calculation
tf_aa_groove = 0.6;    // groove just outside the active area = the window margin

tf_t = tf_thickness;
tf_groups = [for (i = [0 : len(tf_prims) - 1])
                 if (tf_prims[i][1] != "top" && (i == 0 || tf_prims[i - 1][1] != tf_prims[i][1])) tf_prims[i][1]];

assert(tf_slot_d < tf_t - 0.4, "the slot for a bottom-side piece goes through the board");
assert(tf_pocket_z[1] < header_h - tf_min_wall, "the roof over a header pocket is too thin");

// ---- Helpers ----------------------------------------------------------------
// A box whose face at the low end of `axis` has its four edges chamfered.
module tf_box(a, b, axis = -1, c = tf_cham) {
    if (axis < 0) box(a, b);
    else {
        d = [for (k = [0 : 2]) k == axis ? 0 : c];
        e = [for (k = [0 : 2]) k == axis ? c : 0];
        f = [for (k = [0 : 2]) k == axis ? a[k] + eps - b[k] : 0];
        hull() { box(a + d, b - d + f); box(a + e, b); }
    }
}
// Text cut into a face that lies on the bed: mirrored so that it reads from outside.
module tf_text(s, size) text(s, size = size, font = mark_font, halign = "center", valign = "center");

// ---- The stand-ins as the generator gives them ------------------------------------
module tf_prim(p) {
    z0 = p[1] == "top" ? p[7] : -tf_t - p[8];
    if (p[2] == "box") translate([p[3] - p[5] / 2, p[4] - p[6] / 2, z0]) cube([p[5], p[6], p[8] - p[7]]);
    else if (p[0] == "SW3" && abs(p[5] - enc_shaft_d) < 0.01) tf_shaft(p);
    else translate([p[3], p[4], z0]) cylinder(d = p[5], h = p[8] - p[7]);
}
// The encoder shaft with its flat. The flat faces +y, as the knob's bore does.
module tf_shaft(p) {
    r = p[5] / 2;
    translate([p[3], p[4], 0]) difference() {
        translate([0, 0, p[7]]) cylinder(d = p[5], h = p[8] - p[7]);
        translate([-r - 1, tf_shaft_flat[0] - r, p[8] - tf_shaft_flat[1]]) cube([2 * r + 2, r + 1, tf_shaft_flat[1] + 1]);
    }
}
function tf_special(ref) = ref == "J5" || ref == "U4" || len([for (h = tf_headers) if (h[0] == ref) 1]) > 0;

// ---- Terminal headers and plug pieces ------------------------------------------
function tf_pockets(h) = let(inner = h[2] - h[1] - 2 * tf_hdr_wall,
                             n = ceil((inner + tf_hdr_wall) / (tf_pocket_max + tf_hdr_wall)),
                             w = (inner - (n - 1) * tf_hdr_wall) / n)
    [for (i = [0 : n - 1]) [h[1] + tf_hdr_wall + i * (w + tf_hdr_wall), w]];

// The part of a header that is over the board, with pockets in its face.
module tf_header(h) difference() {
    box([h[1], 0, 0], [h[2], h[4], h[5]]);
    for (k = tf_pockets(h))
        box([k[0], -1, tf_pocket_z[0]], [k[0] + k[1], tf_pocket_depth, tf_pocket_z[1]]);
}
// A plug piece: the mated plug, the 2 mm of header beyond the board edge,
// and the prongs. i is the row of tf_headers and of tf_plugs.
module tf_plug(i, mark = true) {
    h = tf_headers[i];
    p = tf_plugs[i];
    difference() {
        union() {
            tf_box([p[1], p[3], p[5]], [p[2], p[4], p[6]], 1);
            box([h[1], h[3] - eps, 0], [h[2], 0, h[5]]);
            for (k = tf_pockets(h)) hull() {
                box([k[0] + tf_prong_fit[0], -eps, tf_pocket_z[0] + tf_prong_fit[1]],
                    [k[0] + k[1] - tf_prong_fit[0], tf_prong_len - 0.5, tf_pocket_z[1] - tf_prong_fit[1]]);
                box([k[0] + tf_prong_fit[0] + 0.5, -eps, tf_pocket_z[0] + tf_prong_fit[1] + 0.5],
                    [k[0] + k[1] - tf_prong_fit[0] - 0.5, tf_prong_len, tf_pocket_z[1] - tf_prong_fit[1] - 0.5]);
            }
        }
        // Pole count on the face the wires enter, which is on the bed.
        if (mark) translate([(p[1] + p[2]) / 2, p[3] + tf_mark, (p[5] + p[6]) / 2]) rotate([90, 0, 0])
            linear_extrude(tf_mark + 1) tf_text(str(h[6], "P"), 4.5);
    }
}

// ---- Main piece -------------------------------------------------------------------
module tf_slab() difference() {
    hull() {
        ext(-tf_t, -tf_t + eps) offset(delta = -tf_cham) polygon(tf_outline);
        ext(-tf_t + tf_cham, 0) polygon(tf_outline);
    }
    for (h = tf_holes) translate([h[1], h[2], 0]) {
        translate([0, 0, -tf_t - 1]) cylinder(d = h[3], h = tf_t + 2);
        translate([0, 0, -tf_t - eps]) cylinder(d1 = h[3] + 2 * tf_cham, d2 = h[3], h = tf_cham);
    }
}
module tf_usb() {
    u = tf_usb;
    o = -u[2];   // how far the mouth is beyond the board edge
    translate([u[0], 0, 0]) rotate([90, 0, 90]) linear_extrude(u[1] - u[0])
        polygon([[0, 0], [u[3], 0], [u[3], u[4]], [u[2], u[4]], [u[2], max(o, 0)]]);
}
module tf_module() {
    m = tf_module;
    box([m[0], m[2], 0], [m[1], m[3], m[4]]);
    // Under the antenna end, beyond the board edge: filled, so that it prints.
    if (m[3] > board_h) hull() {
        box([m[0] + tf_cham, board_h - 1, -tf_t], [m[1] - tf_cham, m[3] - tf_cham, -tf_t + eps]);
        box([m[0], board_h - 1, -tf_t + tf_cham], [m[1], m[3], eps]);
    }
}
// Rail of a bottom-side piece, and with grow > 0 the slot it fits.
module tf_rail(g, grow = 0) {
    b = [for (p = tf_prims) if (p[1] == g && p[2] == "box") p][0];
    along_x = b[5] >= b[6];
    len_ = (along_x ? b[5] : b[6]) - tf_rail_short;
    z0 = -tf_t - eps;
    z1 = -tf_t + (grow > 0 ? tf_slot_d : tf_rail_h);
    if (along_x) box([b[3] - b[5] / 2 - grow, b[4] - tf_rail_w / 2 - grow, z0], [b[3] - b[5] / 2 + len_ + grow, b[4] + tf_rail_w / 2 + grow, z1]);
    else box([b[3] - tf_rail_w / 2 - grow, b[4] - b[6] / 2 - grow, z0], [b[3] + tf_rail_w / 2 + grow, b[4] - b[6] / 2 + len_ + grow, z1]);
}
// Where the top face is marked: the bare area under the display carrier,
// which no stand-in covers, and the top of the cell holder, which stays in
// view when the carrier is on.
tf_mark_at = [25.5, 90];
module tf_top_marking() {
    translate([tf_mark_at[0], tf_mark_at[1], -tf_mark]) linear_extrude(tf_mark + 1) {
        translate([0, 6]) tf_text("TEST FIT", 6.4);
        translate([0, -2]) tf_text(str("rev ", tf_board_rev, ", contract r", tf_contract_rev), 3.2);
        translate([0, -8]) tf_text("not a circuit board", 3.2);
    }
    b = [for (p = tf_prims) if (p[0] == "BT1" && p[2] == "box") p][0];
    translate([b[3], b[4], b[8] - tf_mark]) linear_extrude(tf_mark + 1) {
        translate([0, 3]) tf_text("TEST FIT", 4.6);
        translate([0, -3.6]) tf_text(str("rev ", tf_board_rev, "  r", tf_contract_rev), 3.4);
    }
}
module tf_bottom_marking() translate([board_w / 2, 62, -tf_t - 1]) linear_extrude(tf_mark + 1) mirror([1, 0]) {
    translate([0, 9]) tf_text("TEST FIT", 9);
    translate([0, -1]) tf_text(str("ZoneFlow board rev ", tf_board_rev, " of ", tf_board_date), 4);
    translate([0, -8]) tf_text(str("mechanical contract r", tf_contract_rev), 4);
    translate([0, -15]) tf_text("not a circuit board", 4);
    translate([0, -22]) tf_text("C1 and K1 go in the two slots", 3.4);
}
module tf_main() difference() {
    union() {
        tf_slab();
        for (p = tf_prims) if (p[1] == "top" && !tf_special(p[0])) tf_prim(p);
        for (h = tf_headers) tf_header(h);
        tf_usb();
        tf_module();
    }
    for (g = tf_groups) tf_rail(g, tf_rail_fit);
    tf_top_marking();
    tf_bottom_marking();
}

// ---- Bottom-side pieces -----------------------------------------------------------
// g is the reference of the part. bed is the axis whose low end lies on the
// bed: the far face (2) for a plain block, an end (0 or 1) for a part with a
// lower box beside it, which would overhang otherwise.
function tf_bed_axis(g) = let(n = len([for (p = tf_prims) if (p[1] == g) 1]),
                              b = [for (p = tf_prims) if (p[1] == g) p][0])
    n == 1 ? 2 : (b[5] >= b[6] ? 0 : 1);
module tf_addon(g, mark = true) {
    ps = [for (p = tf_prims) if (p[1] == g) p];
    b = ps[0];
    ax = tf_bed_axis(g);
    difference() {
        union() {
            tf_box([b[3] - b[5] / 2, b[4] - b[6] / 2, -tf_t - b[8]], [b[3] + b[5] / 2, b[4] + b[6] / 2, -tf_t - b[7]], ax);
            for (i = [1 : 1 : len(ps) - 1]) tf_prim(ps[i]);
            tf_rail(g);
        }
        // Its reference on the face away from the board.
        if (mark) translate([b[3], b[4], -tf_t - b[8] - 1]) linear_extrude(tf_mark + 1) mirror([1, 0]) tf_text(g, 4.5);
    }
}

// ---- Display panel stand-in ---------------------------------------------------------
// The fitted panel's outline and thickness, a groove around the active area
// and the cable as a flat strip of its developed length. The strip lies at
// the back face, tf_panel_exit lower than the real cable leaves the panel.
module tf_panel(cable = true) {
    x0 = disp_x0; x1 = disp_x1; y0 = disp_y0; y1 = disp_y1;
    ax0 = disp_c[0] - disp_aa[0] / 2; ax1 = disp_c[0] + disp_aa[0] / 2;
    ay0 = disp_c[1] - disp_aa[1] / 2; ay1 = disp_c[1] + disp_aa[1] / 2;
    zb = z_panel_bk; zf = z_panel_front;
    tl = tf_panel_tail;
    difference() {
        union() {
            tf_box([x0, y0, zb], [x1, y1, zf], 2);
            if (cable) {
                box([x1 - 1, tf_panel_root_y[0], zb], [x1 + tf_fpc_root_len, tf_panel_root_y[1], zb + tf_fpc_t]);
                box([x1 - 1, tl[2] - tl[1] / 2, zb], [x1 + tl[0], tl[2] + tl[1] / 2, zb + tf_fpc_t]);
            }
        }
        // Groove just outside the active area. The fascia's window is that much larger.
        ext(zf - tf_mark, zf + 1) difference() {
            rect(ax0 - tf_aa_groove, ay0 - tf_aa_groove, ax1 + tf_aa_groove, ay1 + tf_aa_groove);
            rect(ax0, ay0, ax1, ay1);
        }
        translate([disp_c[0], disp_c[1], zf - tf_mark]) linear_extrude(tf_mark + 1) {
            translate([0, 8]) tf_text("TEST FIT", 5.6);
            translate([0, 0.5]) tf_text("active area", 3.6);
            translate([0, -5.5]) tf_text(str(disp_aa[0], " x ", disp_aa[1], " mm"), 3.6);
            translate([0, -11.5]) tf_text("cable >", 3.4);
        }
        // Notches where the mouth of J7 should come to on the cable.
        if (cable) for (s = [-1, 1])
            translate([x1 + tl[0] - tf_fpc_insert - 0.5, tl[2] + s * tl[1] / 2 - 1, zb - 1]) cube([1, 2, tf_fpc_t + 2]);
    }
}

// ---- For the check and the renders ---------------------------------------------------
module tf_addons() for (g = tf_groups) tf_addon(g);
module tf_plugs_all() for (i = [0 : len(tf_headers) - 1]) tf_plug(i);
// Soldered lead ends. Not printed: no drawing gives their trimmed length.
module tf_tails_all() for (t = tf_tails)
    translate([t[1], t[2], t[4] == "top" ? 0 : -tf_t - tf_tail_len]) cylinder(d = t[3], h = tf_tail_len, $fn = 24);

col_tf      = "#e8c36a";
col_tf_add  = "#d99a4e";
col_tf_plug = "#6aa9d9";
col_tf_panel = "#c9cdd3";
module tf_board_colored(plugs = true, panel_ = false) {
    color(col_tf) tf_main();
    color(col_tf_add) tf_addons();
    if (plugs) color(col_tf_plug) tf_plugs_all();
    if (panel_) color(col_tf_panel) tf_panel(cable = false);
}

// ---- Scenes for the test-fit guide ---------------------------------------------------
// The dry fit, step by step: 1 board in the base, 2 carrier and panel,
// 3 fascia with its small parts, 4 plug pieces, 5 wiring cover.
tf_step = 5;
// With tf_cutting, only a slice is shown, so that the gaps between the parts
// can be seen: ["y", 42] is the slice from y = 42 to y = 42 + tf_slice, seen
// from the terminal side; ["x", 16] the slice from x = 16 - tf_slice to
// x = 16, seen from the right.
tf_cutting = false;
tf_cut = ["y", 88.4];
tf_slice = 1.0;
module tf_keep() {
    if (!tf_cutting) children();
    else intersection() {
        union() children();
        if (tf_cut[0] == "y") box([ox0 - 5, tf_cut[1], z_back - 5], [ox1 + 5, tf_cut[1] + tf_slice, knob_z1 + 5]);
        else box([tf_cut[1] - tf_slice, oy0 - 5, z_back - 5], [tf_cut[1], fy1 + 5, knob_z1 + 5]);
    }
}
module tf_scene(step = tf_step) {
    color(col_base) tf_keep() base();
    color(col_tf) tf_keep() tf_main();
    color(col_tf_add) tf_keep() tf_addons();
    if (step >= 2) {
        color(col_bridge) tf_keep() bridge();
        color(col_tf_panel) tf_keep() tf_panel(cable = false);
    }
    if (step >= 3) {
        color(col_front) tf_keep() fascia();
        color(col_pipe, 0.9) tf_keep() lightpipe();
        color(col_btn) tf_keep() for (i = [0, 1]) button(i);
        color(col_knob) tf_keep() knob();
    }
    if (step >= 4) color(col_tf_plug) tf_keep() tf_plugs_all();
    if (step >= 5) color("#4a5057") tf_keep() cover();
}
// The pieces of the kit, spread out.
module tf_kit() {
    color(col_tf) tf_main();
    color(col_tf_add) translate([0, 0, -22]) tf_addons();
    color(col_tf_plug) translate([0, -14, 0]) tf_plugs_all();
    color(col_tf_panel) translate([0, 0, 28]) tf_panel();
}
// The underside with its two slots, turned over so that the lettering reads.
module tf_underside() translate([board_w, 0, 0]) rotate([0, 180, 0]) color(col_tf) tf_main();

// ---- Print orientation -------------------------------------------------------------
// Each piece lies with its lowest corner at the origin.
module tf_print_main() translate([0, -min(tf_usb[2], 0), tf_t]) tf_main();
module tf_print_addon(g) {
    b = [for (p = tf_prims) if (p[1] == g) p][0];
    ax = tf_bed_axis(g);
    lo = [b[3] - b[5] / 2, b[4] - b[6] / 2, -tf_t - b[8]];
    if (ax == 2) translate([-lo[0], -lo[1], -lo[2]]) tf_addon(g);
    else if (ax == 0) translate([tf_rail_h - tf_t, -lo[1], -lo[0]]) rotate([0, -90, 0]) tf_addon(g);
    else translate([-lo[0], tf_rail_h - tf_t, -lo[1]]) rotate([90, 0, 0]) tf_addon(g);
}
// Standing on the face the wires enter, prongs up.
module tf_print_plug(i) {
    p = tf_plugs[i];
    translate([-p[1], p[6], -p[3]]) rotate([90, 0, 0]) tf_plug(i);
}
module tf_print_panel() translate([-disp_x0, -disp_y0, -z_panel_bk]) tf_panel();
