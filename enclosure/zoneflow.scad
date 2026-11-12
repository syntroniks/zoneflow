// ZoneFlow enclosure, revision A concept.
//
// Wall-mounted, two compartments: electronics above, wiring below.
// Eight printed parts: base, fascia, wiring cover, display carrier, knob,
// two button caps and light-pipe comb.
//
// Coordinates are the board's (hardware/mechanical.yaml): origin at the
// bottom-left board corner seen from the front, x right, y up, z towards the
// user, z = 0 at the board's top surface. Parts are modelled in place and
// turned into print orientation only for export.
//
// Build everything with `make` in this directory. To look at one thing:
//   openscad -D 'part="fascia"' zoneflow.scad

include <board_params.scad>
include <params.scad>
// The fonts for the engraved markings travel with the source, so that the
// exported parts do not depend on what the build machine has installed.
use <fonts/LiberationSans-Bold.ttf>
use <fonts/LiberationSans-Regular.ttf>

// What to show. See the list at the bottom of this file.
part = "assembly";
// For part = "check": the two solids to intersect.
check_a = "fascia";
check_b = "board";
// For renders: stand the product up as it hangs on a wall (+y becomes +z).
wallview = false;

// ---- Derived dimensions -------------------------------------------------
ox0 = -(pcb_gap + wall);                 // outside, left
ox1 = board_w + pcb_gap + wall;          // outside, right
oy1 = board_h + top_gap + wall;          // outside of the base, top
part_y0 = header_face_y;                 // partition, wiring side
part_y1 = part_y0 + part_wall;           // partition, board side
oy0 = part_y0 - bay_h - wall;            // outside, bottom
ix0 = ox0 + wall;
ix1 = ox1 - wall;
iy0 = oy0 + wall;
iy1 = oy1 - wall;
roof_rise = roof_gap + roof_t;
fy1 = oy1 + roof_rise;                   // top of the fascia, over the roof gap
z_pcb_bot = -board_t;
z_floor = z_pcb_bot - bottom_clearance;  // inside of the back wall
z_back = z_floor - back_t;               // the surface against the wall
zF = z_fascia_outer;
zFi = z_fascia_inner;
zC = zF + cover_proud;
zCi = z_cover_inner;
cover_y1 = part_y0 - reveal;             // top wall of the wiring cover
brow_y = part_y0 + cover_overlap;        // top edge of its flap over the fascia
z_brow = zF + 0.15;                      // underside of that flap
led_y = led_c[0][1];
knob_z0 = zF + knob_gap;
knob_z1 = enc_shaft_top_z + knob_roof;
eps = 0.01;

// The display panel in use: a row of panel_options from the contract.
P = [for (p = panel_options) if (p[0] == panel) p][0];
disp_aa = [P[1], P[2]];
disp_outline = [P[3], P[4]];
disp_c = [P[8], P[9]];
disp_x0 = disp_c[0] - disp_aa[0] / 2 - P[5];
disp_y0 = disp_c[1] - disp_aa[1] / 2 - P[6];
disp_x1 = disp_x0 + disp_outline[0];
disp_y1 = disp_y0 + disp_outline[1];
disp_fpc_root_y = [disp_y0 + P[10], disp_y0 + P[11]];
z_panel_bk = z_panel_front - P[7];
z_deck_top = z_panel_bk - panel_pad;
z_deck_bot = z_deck_top - deck_t;

assert(abs(part_y1 + part_gap) < eps, "partition does not sit part_gap from the board edge");
assert(abs(zC - skin - zCi) < eps, "cover_proud and z_cover_inner disagree");
assert(part_gap + part_wall <= -header_face_y + eps, "partition is thicker than the header overhang");
assert(panel != panel_fitted || abs(z_panel_bk - z_panel_back) < eps, "panel thickness and stack disagree");
assert(brow_y > fuse_well[3] + 1, "the cover flap does not reach past the fuse well");

// ---- Helpers --------------------------------------------------------------
module box(a, b) translate(a) cube(b - a);
module ext(z0, z1) translate([0, 0, z0]) linear_extrude(z1 - z0) children();
module rrect(x0, y0, x1, y1, r)
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r]) translate([x, y]) circle(r);
module rect(x0, y0, x1, y1) translate([x0, y0]) square([x1 - x0, y1 - y0]);
module pill(s) hull() for (sx = [-1, 1]) translate([sx * (s[0] - s[1]) / 2, 0]) circle(d = s[1]);
// A slab with a 45 degree chamfer around its upper edge. Child must be convex.
module chamfer_slab(z0, z1, c) hull() {
    ext(z0, z1 - c) children();
    ext(z1 - eps, z1) offset(delta = -c) children();
}
module along_y(p, d, len) translate(p) rotate([90, 0, 0]) cylinder(d = d, h = len);
module mirror_x() translate([board_w / 2, 0, 0]) mirror([1, 0, 0]) translate([-board_w / 2, 0, 0]) children();

module outer2d() rrect(ox0, oy0, ox1, oy1, corner_r);
module inner2d() offset(delta = -wall) outer2d();
// The fascia is taller than the base by the roof and its gap.
module fascia_outer2d() rrect(ox0, oy0, ox1, fy1, corner_r);
module fascia_clip2d() rect(ox0 - 1, part_y0, ox1 + 1, fy1 + 1);
module cover_clip2d() rect(ox0 - 1, oy0 - 1, ox1 + 1, cover_y1);
module cover2d() intersection() { outer2d(); cover_clip2d(); }

// Threaded hole for an M3 screw, pointing down -z from the origin.
module fastener_hole() {
    if (fastener == "insert") translate([0, 0, -insert_depth]) cylinder(d = insert_d, h = insert_depth + eps);
    else translate([0, 0, -selftap_depth]) cylinder(d = selftap_d, h = selftap_depth + eps);
}

// The tongue a front part pushes inside the base walls. It hangs over the
// board edge, so it starts at lip_z0. Above z_split it widens at 45 degrees
// into the skirt so that it prints without support.
module lip() {
    ext(lip_z0, z_split) difference() {
        offset(delta = -(wall + fit)) outer2d();
        offset(delta = -(wall + fit + lip_t)) outer2d();
    }
    difference() {
        ext(z_split - eps, z_split + fit + lip_t) inner2d();
        hull() {
            ext(z_split - 2 * eps, z_split - eps) offset(delta = -(wall + fit + lip_t)) outer2d();
            ext(z_split + fit + lip_t, z_split + fit + lip_t + eps) inner2d();
        }
    }
}

// ---- Base -----------------------------------------------------------------
function entry_zb(w) = lip_z0 - (w + 0.6);   // bottom of a cable entry
function entry_zc(w) = entry_zb(w) + w / 2;
function near(x, list, d) = len([for (v = list) if (abs(v - x) < d) 1]) > 0;

module entry_channel(x, w, y0, y1) hull() {
    along_y([x, y1, entry_zc(w)], w, y1 - y0);
    box([x - w / 2, y0, z_split], [x + w / 2, y1, z_split + 1]);
}

module boss(x, y, z1, d = boss_d) translate([x, y, 0]) {
    translate([0, 0, z_floor - eps]) cylinder(d = d, h = z1 - z_floor + eps);
    translate([0, 0, z_floor - eps]) cylinder(d1 = d + 2, d2 = d, h = 3);
}

module keyhole2d() {
    circle(d = keyhole_big);
    hull() { circle(d = keyhole_small); translate([0, keyhole_slot]) circle(d = keyhole_small); }
}

// Slits through a side wall that slope down towards the outside, so that
// water running down the wall cannot follow them in.
module louvres() {
    ty = vent_gap * sqrt(2);
    run = wall + 0.2;
    for (g = louvre_groups, y = [g[0] : louvre_pitch : g[1]])
        ext(louvre_z[0], louvre_z[1])
            polygon([[ox0 - 0.1, y], [ox0 - 0.1, y + ty], [ix0 + 0.1, y + ty + run], [ix0 + 0.1, y + run]]);
}

module base() {
    difference() {
        union() {
            // Tray.
            difference() {
                hull() {
                    ext(z_back, z_back + eps) offset(delta = -back_chamfer) outer2d();
                    ext(z_back + back_chamfer, z_split) outer2d();
                }
                ext(z_floor, z_split + 1) inner2d();
            }
            // Partition between the compartments, up to just under the headers.
            box([ix0 - eps, part_y0, z_floor - eps], [ix1 + eps, part_y1, -0.3]);
            // Board bosses and the support post.
            for (h = holes) {
                if (h[4] == "screw") boss(h[1], h[2], z_pcb_bot);
                if (h[4] == "post") boss(h[1], h[2], z_pcb_bot, 5);
            }
            // Closed pockets over the keyholes, so a screw head cannot reach
            // the board and no hole opens into the electronics.
            for (x = keyhole_x)
                difference() {
                    box([x - 7.6, keyhole_y - 6.4, z_floor - eps],
                        [x + 7.6, keyhole_y + keyhole_slot + 6.4, z_floor + keyhole_head_h + 1.2]);
                    box([x - 6, keyhole_y - 4.8, z_floor - 1],
                        [x + 6, keyhole_y + keyhole_slot + 4.8, z_floor + keyhole_head_h]);
                }
            // Boss for the cover screw, tied to the bottom wall.
            boss(cover_screw[0], cover_screw[1], zCi - 1.5, 9);
            box([cover_screw[0] - 1.5, iy0 - eps, z_floor - eps], [cover_screw[0] + 1.5, cover_screw[1], 0.2]);
            // Cable cradles with a tunnel for a cable tie.
            for (e = entry)
                box([e[0] - e[1] / 2 - 3, iy0 - eps, z_floor - eps], [e[0] + e[1] / 2 + 3, iy0 + 7, entry_zc(e[1])]);
            // Two ribs that hold the capacitor C1, which lies on its side
            // under the board, against the board.
            for (s = [-1, 1])
                box([c1_c[0] + s * c1_rib_dx - 1, c1_c[1] - 5, z_floor - eps],
                    [c1_c[0] + s * c1_rib_dx + 1, c1_c[1] + 5, z_pcb_bot - c1_can[1] - c1_rib_gap]);
        }
        // Board screws.
        for (h = holes) if (h[4] == "screw") translate([h[1], h[2], z_pcb_bot]) fastener_hole();
        translate([cover_screw[0], cover_screw[1], zCi - 1.5]) fastener_hole();
        // Keyholes and the lower fixing slot.
        for (x = keyhole_x) translate([x, keyhole_y]) ext(z_back - 1, z_floor + eps) keyhole2d();
        translate(lower_fix) ext(z_back - 1, z_floor + 1)
            hull() for (dy = [-2.5, 2.5]) translate([0, dy]) circle(d = keyhole_small);
        // Rear knock-outs: a groove that leaves a thin skin.
        for (k = rear_ko) translate(k) ext(z_back + rear_ko_skin, z_floor + 1)
            difference() { circle(d = rear_ko_d + 1.6); circle(d = rear_ko_d); }
        // Bottom cable entries. A thin web stays in each until it is cut out.
        for (e = entry) {
            entry_channel(e[0], e[1], oy0 + entry_web, iy0 + 7 + eps);
            box([e[0] - e[1] / 2 - 4, iy0 + 1.5, entry_zb(e[1]) - 2.0 - tie_slot[1]],
                [e[0] + e[1] / 2 + 4, iy0 + 1.5 + tie_slot[0], entry_zb(e[1]) - 2.0]);
        }
        // USB plug passes the partition here.
        box([usb_x - 6.5, part_y0 - 1, usb_z - 3.7], [terminals[0][1] - comp_clear, part_y1 + 1, 1]);
        // Slots that the fascia hooks latch into.
        for (x = hook_x)
            box([x - hook_w / 2 - 0.5, part_y0 - 1, -4.9], [x + hook_w / 2 + 0.5, part_y1 + 1, -1.7]);
        // Fascia screws pass the top wall.
        for (x = top_screw_x) along_y([x, oy1 + 1, top_screw_z], m3_clear, wall + 2);
        // Ventilation. Inlets: slanted slits low in both side walls, slits
        // in the bottom wall and in the partition below the board. Outlets:
        // slanted slits high in the side walls, and slits in the top wall
        // under the fascia's roof.
        louvres();
        mirror_x() louvres();
        for (x = bottom_slit_x)
            box([x - vent_gap / 2, oy0 - 1, bottom_slit_z[0]], [x + vent_gap / 2, iy0 + 1, bottom_slit_z[1]]);
        for (x = [ix0 + 4 : part_slit_pitch : ix1 - 4]) if (!near(x, hook_x, hook_w + 2))
            box([x - vent_gap / 2, part_y0 - 1, part_slit_z[0]], [x + vent_gap / 2, part_y1 + 1, part_slit_z[1]]);
        for (x = [ox0 + corner_r + 3 : top_slit_pitch : ox1 - corner_r - 3]) if (!near(x, top_screw_x, 5.5))
            box([x - vent_gap / 2, iy1 - 1, top_slit_z[0]], [x + vent_gap / 2, oy1 + 1, top_slit_z[1]]);
    }
}

// ---- Fascia ---------------------------------------------------------------
module window2d(grow) rect(disp_c[0] - disp_aa[0] / 2 - disp_window_margin - grow,
                           disp_c[1] - disp_aa[1] / 2 - disp_window_margin - grow,
                           disp_c[0] + disp_aa[0] / 2 + disp_window_margin + grow,
                           disp_c[1] + disp_aa[1] / 2 + disp_window_margin + grow);

module hook(x) {
    y_out = part_y0 - 2.2;
    y_in = part_y0 - 0.2;
    w2 = hook_w / 2;
    // Tab on the wiring side of the base partition.
    box([x - w2, y_out, -4.6], [x + w2, y_in, 0.3 + eps]);
    box([x - w2, y_out, 0.3], [x + w2, part_y0 + eps, 6.0]);
    // 45 degree gusset back to the fascia's own partition wall.
    hull() {
        box([x - w2, y_out, 6.0 - eps], [x + w2, part_y0 + eps, 6.0]);
        box([x - w2, part_y0, 6.0], [x + w2, part_y0 + eps, 8.2]);
    }
    // Barb: 45 degree catch above, lead-in below.
    hull() {
        box([x - w2, y_in - eps, -4.5], [x + w2, y_in, -2.0]);
        box([x - w2, y_in, -3.25], [x + w2, y_in + 1.2, -3.15]);
    }
}

module fascia() {
    cx = (disp_x0 + disp_x1) / 2;
    cy = (disp_y0 + disp_y1) / 2;
    f = display_fit;
    fw = fuse_well;
    difference() {
        union() {
            difference() {
                intersection() {
                    union() {
                        chamfer_slab(z_split + reveal_h, zF, chamfer) fascia_outer2d();
                        ext(z_split, z_split + reveal_h + eps) offset(delta = -reveal) fascia_outer2d();
                    }
                    ext(z_split - 1, zF + 1) fascia_clip2d();
                }
                ext(z_split - 1, zFi) intersection() { offset(delta = -wall) fascia_outer2d(); fascia_clip2d(); }
            }
            intersection() { lip(); box([ox0, part_y1, lip_z0 - 1], [ox1, oy1, zF]); }
            // Inner top wall, standing on the base's top wall. It closes the
            // electronics compartment; the space above it is the roof gap.
            ext(z_split, zFi + eps) intersection() {
                difference() { outer2d(); inner2d(); }
                rect(ox0, oy1 - corner_r, ox1, oy1);
            }
            // Roof: the fascia's top wall carried back over the base, with a
            // gap under it. Drips land on it; air leaves behind and beside it.
            box([ox0 + corner_r, oy1 + roof_gap, roof_z0], [ox1 - corner_r, fy1, z_split + reveal_h + eps]);
            // Bosses that carry the two fascia screws through the roof gap.
            for (x = top_screw_x) {
                box([x - 4, oy1 + 0.15, top_screw_z - 4], [x + 4, oy1 + roof_gap + eps, z_split + eps]);
                box([x - 4, oy1 - eps, z_split], [x + 4, oy1 + roof_gap + eps, zFi + eps]);
            }
            // Its half of the partition, with teeth between the connectors.
            box([ix0 - eps, part_y0, 0.3], [ix1 + eps, part_y1, zFi + eps]);
            // Pillars that take the two screws.
            for (x = top_screw_x) box([x - 4, iy1 - fit - 7.25, lip_z0], [x + 4, iy1 - fit, zFi + eps]);
            // Ribs that locate the display panel sideways.
            box([disp_x0 - f - rib_t, cy - 10, rib_z0], [disp_x0 - f, cy + 10, zFi + eps]);
            box([cx - 12, disp_y1 + f, rib_z0], [cx + 12, disp_y1 + f + rib_t, zFi + eps]);
            box([btn_c[0][0] + 10.6, disp_y0 - f - rib_t, rib_z0], [btn_c[1][0] - 10.6, disp_y0 - f, zFi + eps]);
            for (y = [disp_y0 + 0.5, disp_y1 - 5.5])
                box([disp_x1 + f, y, rib_z0], [disp_x1 + f + rib_t, y + 5, zFi + eps]);
            // Channel that grips the light-pipe rail.
            for (s = [-1, 1]) {
                y = led_y + s * (lp_size / 2 + lp_fit + lp_chan_wall / 2);
                box([led_c[0][0] - 2.5, y - lp_chan_wall / 2, lp_rail_z[0] - 0.2],
                    [led_c[len(led_c) - 1][0] + 2.5, y + lp_chan_wall / 2, zFi + eps]);
            }
            // Walls of the fuse well, down to just above the board.
            box([fw[0], fw[1], 0.3], [fw[2], fw[3], zFi + eps]);
            for (x = hook_x) hook(x);
        }
        // Display: pocket for the panel, then the window with its chamfer.
        ext(zFi - eps, zF - bezel_lip) rect(disp_x0 - f, disp_y0 - f, disp_x1 + f, disp_y1 + f);
        hull() {
            ext(zF - bezel_lip - eps, zF - bezel_lip) window2d(0);
            ext(zF, zF + eps) window2d(bezel_cham);
        }
        // Encoder, buttons, light pipes.
        translate(enc_c) ext(zFi - 1, zF + 1) circle(d = enc_hole_d);
        for (c = btn_c) translate(c) ext(zFi - 1, zF + 1) offset(delta = fit) pill(btn_size);
        for (c = led_c) translate(c) ext(zFi - 1, zF + 1) square(lp_hole, center = true);
        // Fuse well: open to the front, where the wiring cover's flap closes it.
        box([fw[0] + 1.2, fw[1] + 1.2, 0], [fw[2] - 1.2, fw[3] - 1.2, zF + 1]);
        // Markings.
        for (i = [0 : len(led_c) - 1]) if (led_label[i] != "")
            translate([led_c[i][0], mark_y, zF - mark_depth]) linear_extrude(1)
                text(led_label[i], size = mark_size, font = mark_font, halign = "center", valign = "center");
        translate(led_c[0]) ext(zF - 0.3, zF + 1) difference() { circle(d = 6.4); circle(d = 5.2); }
        translate([wordmark_at[0], wordmark_at[1], zF - mark_depth]) linear_extrude(1)
            text(wordmark, size = wordmark_size, font = mark_font, halign = "right", valign = "center", spacing = 1.05);
        translate([fw[2] + 3, (fw[1] + fw[3]) / 2, zF - mark_depth]) linear_extrude(1)
            text(fuse_text, size = 3.2, font = mark_font, valign = "center");
        // Openings in the partition for the edge connectors.
        for (t = terminals)
            box([t[1] - comp_clear, part_y0 - 1, -1], [t[2] + comp_clear, part_y1 + 1, header_h + comp_clear]);
        box([usb_x - 6.5, part_y0 - 1, -1], [terminals[0][1] - comp_clear + eps, part_y1 + 1, usb_z + 3.7]);
        box([qwiic_x - 3.75, part_y0 - 1, -1], [qwiic_x + 3.75, part_y1 + 1, 4.4]);
        // Slots for the wiring cover's toes.
        for (x = toe_x)
            box([x - toe_w / 2 - fit, part_y0 - 1, 6.9], [x + toe_w / 2 + fit, part_y1 + 1, 8.75]);
        // Screws: threaded hole in each pillar, clearance through boss and
        // roof, countersink in the roof.
        for (x = top_screw_x) {
            translate([x, iy1 - fit, top_screw_z]) rotate([-90, 0, 0]) fastener_hole();
            along_y([x, fy1 + 1, top_screw_z], m3_clear, roof_rise + 1.2);
            translate([x, fy1 + eps, top_screw_z]) rotate([90, 0, 0])
                cylinder(d1 = m3_csk_d, d2 = m3_clear, h = (m3_csk_d - m3_clear) / 2);
        }
    }
}

// ---- Wiring cover -----------------------------------------------------------
module cover() {
    wall_y0 = cover_y1 - 2.0;
    fc = flap_cham;
    fx0 = ox0 + chamfer + 0.3;
    fx1 = ox1 - chamfer - 0.3;
    difference() {
        union() {
            difference() {
                union() {
                    chamfer_slab(z_split + reveal_h, zC, chamfer) cover2d();
                    ext(z_split, z_split + reveal_h + eps) intersection() { offset(delta = -reveal) outer2d(); cover_clip2d(); }
                }
                ext(z_split - 1, zCi) intersection() { inner2d(); rect(ox0, oy0, ox1, wall_y0); }
                // The top wall stops above the plugs.
                box([ix0, wall_y0 - eps, z_split - 1], [ix1, cover_y1 + 1, plug_top_z + 0.3]);
            }
            // Flap that lies on the fascia. It closes the fuse well, hides
            // the seam and holds the fascia's lower edge down.
            hull() {
                box([fx0, cover_y1 - chamfer, z_brow], [fx1, brow_y, zC - fc]);
                box([fx0 + fc, cover_y1 - chamfer, z_brow], [fx1 - fc, brow_y - fc, zC]);
            }
            intersection() { lip(); box([ox0, oy0, lip_z0 - 1], [ox1, wall_y0 - 1.4, zC]); }
            // Toes that hook into the fascia.
            for (x = toe_x) {
                box([x - toe_w / 2, wall_y0, 6.6], [x + toe_w / 2, cover_y1, zCi + eps]);
                hull() {
                    box([x - toe_w / 2, cover_y1 - eps, 7.2], [x + toe_w / 2, cover_y1, 9.2]);
                    box([x - toe_w / 2, cover_y1 + 1.99, 7.2], [x + toe_w / 2, cover_y1 + 2.0, 7.3]);
                }
            }
            translate([cover_screw[0], cover_screw[1], zCi - 1.4]) cylinder(d = 9, h = 1.4 + eps);
        }
        translate([cover_screw[0], cover_screw[1], zCi - 2]) cylinder(d = m3_clear, h = skin + 3);
        translate([cover_screw[0], cover_screw[1], zC - (m3_csk_d - m3_clear) / 2])
            cylinder(d1 = m3_clear, d2 = m3_csk_d, h = (m3_csk_d - m3_clear) / 2 + eps);
        // Recess for the label with the zone names.
        ext(zC - label_depth, zC + 1) rect(board_w / 2 - label_size[0] / 2, label_y0,
                                         board_w / 2 + label_size[0] / 2, label_y0 + label_size[1]);
    }
}

// ---- Display carrier --------------------------------------------------------
module bridge() {
    dx0 = disp_x0 - deck_margin;
    dx1 = disp_x1 + deck_margin;
    dy0 = disp_y0 - deck_margin;
    dy1 = disp_y1 + deck_margin;
    difference() {
        union() {
            box([dx0, dy0, z_deck_bot], [dx1, dy1, z_deck_top]);
            for (h = holes) if (h[4] == "screw" && h[5] > 8 || h[4] == "foot") {
                ax = min(max(h[1], dx0 + 2), dx1 - 2);
                ay = min(max(h[2], dy0 + 3), dy1 - 3);
                if (h[4] == "screw") translate([h[1], h[2], 0]) cylinder(d = leg_d, h = z_deck_top);
                else box([h[1] - foot_size / 2, h[2] - foot_size / 2, 0], [h[1] + foot_size / 2, h[2] + foot_size / 2, z_deck_top]);
                hull() {
                    translate([h[1], h[2], z_deck_bot]) cylinder(d = h[4] == "screw" ? leg_d : foot_size, h = deck_t);
                    box([ax - 2, ay - 3, z_deck_bot], [ax + 2, ay + 3, z_deck_top]);
                }
            }
        }
        // Opening through which the FPC is plugged into J7 and folds.
        box([disp_x1 - 8.2, disp_fpc_root_y[0] - 0.7, z_deck_bot - 1], [dx1 + 1, disp_fpc_root_y[1] + 0.85, z_deck_top + 1]);
        for (h = holes) if (h[4] == "screw" && h[5] > 8) translate([h[1], h[2], 0]) {
            translate([0, 0, -1]) cylinder(d = m3_clear, h = z_deck_top + 2);
            // Countersunk, so that the part prints deck-down without support.
            translate([0, 0, z_deck_top - (m3_csk_d - m3_clear) / 2])
                cylinder(d1 = m3_clear, d2 = m3_csk_d, h = (m3_csk_d - m3_clear) / 2 + eps);
        }
    }
}

// ---- Knob -------------------------------------------------------------------
// A low drum on a wider foot. Twelve shallow scallops run up the side and
// through the top chamfer, so the top reads as a twelve-sided flower. There
// is no pointer: the encoder turns without end. Printed top down; nothing
// overhangs more than 45 degrees.
module knob() {
    hub_z0 = enc_bushing_top_z + enc_travel + 1.6;
    r = enc_shaft_d / 2 + knob_bore_fit;
    z_sk = knob_z0 + knob_skirt_h;                                   // top of the foot's rim
    z_w = z_sk + (knob_skirt_d - knob_body_d[0]) / 2;                // waist, after a 45 degree cone
    translate([enc_c[0], enc_c[1], 0]) {
        difference() {
            union() {
                translate([0, 0, knob_z0]) cylinder(d = knob_skirt_d, h = knob_skirt_h, $fn = 96);
                translate([0, 0, z_sk - eps]) cylinder(d1 = knob_skirt_d, d2 = knob_body_d[0], h = z_w - z_sk + eps, $fn = 96);
                hull() {
                    translate([0, 0, z_w - eps]) cylinder(d = knob_body_d[0], h = eps, $fn = 96);
                    translate([0, 0, knob_z1 - knob_top_cham]) cylinder(d = knob_body_d[1], h = eps, $fn = 96);
                    translate([0, 0, knob_z1 - eps]) cylinder(d = knob_body_d[1] - 2 * knob_top_cham, h = eps, $fn = 96);
                }
            }
            // Hollow underside.
            translate([0, 0, knob_z0 - eps]) cylinder(d = knob_body_d[1] - 2 * knob_wall - 2 * knob_scallop_depth, h = knob_z1 - knob_roof - knob_z0);
            // Scallops, parallel to the tapered side. Each fades out over
            // 2 mm above the foot, so that nothing overhangs in the print.
            for (i = [0 : knob_scallops - 1]) rotate(i * 360 / knob_scallops) {
                r0 = knob_body_d[0] / 2 + knob_scallop_d / 2;
                r1 = knob_body_d[1] / 2 + knob_scallop_d / 2 - knob_scallop_depth;
                hull() {
                    translate([r0 + 0.2, 0, z_w]) cylinder(d = knob_scallop_d, h = eps);
                    translate([r0 - knob_scallop_depth, 0, z_w + 2]) cylinder(d = knob_scallop_d, h = eps);
                }
                hull() {
                    translate([r0 - knob_scallop_depth, 0, z_w + 2]) cylinder(d = knob_scallop_d, h = eps);
                    translate([r1, 0, knob_z1 + 1]) cylinder(d = knob_scallop_d, h = eps);
                }
            }
            // Engraved ring on the top face.
            translate([0, 0, knob_z1 - 0.4]) linear_extrude(1) difference() { circle(d = knob_ring_d); circle(d = knob_ring_d - 1.6); }
        }
        difference() {
            translate([0, 0, hub_z0]) cylinder(d = 11, h = knob_z1 - knob_roof - hub_z0 + eps);
            // D bore for the 6 mm shaft with its flat.
            translate([0, 0, hub_z0 - 1]) linear_extrude(enc_shaft_top_z + 0.3 - hub_z0 + 1) intersection() {
                circle(r = r);
                translate([-r - 1, -r - 1]) square([2 * r + 2, r + 1 + enc_flat_across - enc_shaft_d / 2 + knob_bore_fit]);
            }
        }
    }
}

// ---- Button cap ---------------------------------------------------------------
// Back is a chevron, Home / Manual a ring. The screen names both keys in
// its bottom row, directly above them.
module button_icon(i) {
    if (i == 0) polygon([[1.7, 2.3], [-0.5, 0], [1.7, -2.3], [0.6, -2.3], [-1.6, 0], [0.6, 2.3]]);
    else difference() { circle(d = 4.4); circle(d = 2.9); }
}
module button(i) {
    zt = zF + btn_proud;
    zf1 = zFi - btn_flange_gap;
    translate([btn_c[i][0], btn_c[i][1], 0]) difference() {
        union() {
            ext(btn_flange_z0, zf1) offset(delta = btn_flange) pill(btn_size);
            ext(zf1 - eps, zt - 0.6) pill(btn_size);
            hull() {
                ext(zt - 0.6 - eps, zt - 0.6) pill(btn_size);
                ext(zt - eps, zt) offset(delta = -0.6) pill(btn_size);
            }
        }
        translate([0, 0, btn_flange_z0 - eps]) cylinder(d = btn_socket_d, h = btn_top_z - btn_flange_z0 + eps);
        translate([0, 0, zt - mark_depth]) linear_extrude(1) button_icon(i);
    }
}

// ---- Light-pipe comb ----------------------------------------------------------
module lightpipe() {
    h = lp_size / 2;
    for (c = led_c) box([c[0] - h, led_y - h, lp_bottom_z], [c[0] + h, led_y + h, zF]);
    box([led_c[0][0] - h, led_y - h, lp_rail_z[0]], [led_c[len(led_c) - 1][0] + h, led_y + h, lp_rail_z[1]]);
}

// ---- Placeholder board ----------------------------------------------------------
module prim(p) {
    z0 = p[2] == "top" ? p[8] : -board_t - p[9];
    if (p[3] == "box") translate([p[4] - p[6] / 2, p[5] - p[7] / 2, z0]) cube([p[6], p[7], p[9] - p[8]]);
    else translate([p[4], p[5], z0]) cylinder(d = p[6], h = p[9] - p[8]);
}
module pcb() difference() {
    ext(-board_t, 0) polygon(board_outline);
    for (h = holes) if (h[3] > 0) translate([h[1], h[2], -board_t - 1]) cylinder(d = h[3], h = board_t + 2);
}
module board_parts(group) for (p = part_prims) if (p[1] == group) prim(p);
// The display panel and the space its folded cable sweeps. For the fitted
// panel these come from the contract. For another panel option the cable is
// a guess: a box from the panel edge down to the board.
module panel_solid() {
    if (panel == panel_fitted) for (p = part_prims) if (p[0] == "DS1") prim(p);
    else box([disp_x0, disp_y0, z_panel_bk], [disp_x1, disp_y1, z_panel_front]);
}
module fpc_solid() {
    if (panel == panel_fitted) for (p = part_prims) if (p[0] == "DS1-FPC") prim(p);
    else box([disp_x1 + 0.05, disp_fpc_root_y[0], 0.6], [disp_x1 + 6, disp_fpc_root_y[1], z_panel_front - 1]);
}
module display_colored() {
    color(col_glass) panel_solid();
    color("#c98a2b", 0.8) fpc_solid();
    // Lit area of the display, for the renders.
    color("#1b6fb5") translate([disp_c[0] - disp_aa[0] / 2, disp_c[1] - disp_aa[1] / 2, z_panel_front - 0.02])
        cube([disp_aa[0], disp_aa[1], 0.04]);
}
module board_colored(display = true) {
    color(col_pcb) pcb();
    color(col_part) board_parts("board");
    color(col_mate) board_parts("mate");
    if (display) display_colored();
}

// The volume the contract promises is free for components: every height zone
// as a solid, later zones overriding earlier ones, screw keep-outs removed.
module zone_col(z) box([z[2], z[3], -100], [z[4], z[5], 100]);
module zone_free() {
    for (i = [0 : len(zones) - 1]) {
        z = zones[i];
        if (z[6] > 0) difference() {
            if (z[1] == "top") box([z[2], z[3], 0], [z[4], z[5], z[6]]);
            else box([z[2], z[3], -board_t - z[6]], [z[4], z[5], -board_t]);
            for (j = [i + 1 : 1 : len(zones) - 1]) if (zones[j][1] == z[1]) zone_col(zones[j]);
            for (h = holes) {
                if (z[1] == "top" && h[5] > 0) translate([h[1], h[2], -1]) cylinder(d = h[5], h = 50);
                if (z[1] == "bottom" && h[6] > 0) translate([h[1], h[2], -50]) cylinder(d = h[6], h = 50);
            }
        }
    }
}

// ---- Test-fit kit -------------------------------------------------------------------
// Printable stand-ins for the board, its plugs and the display panel.
include <testfit_params.scad>
include <testfit.scad>

// ---- Assembly and views -----------------------------------------------------------
// Render only: a paper label in the recess and the markings filled with
// white paint, which is how the printing guide says to finish them.
zone_names = ["1  Front lawn", "2  Back lawn", "3  Rose bed", "4  Vegetables",
              "5  Hedge", "6  Drip line", "7  Pots", "8  Spare"];
module label_dressing() {
    lx = board_w / 2 - label_size[0] / 2 + 0.6;
    color("#f3f0e8") box([lx, label_y0 + 0.6, zC - label_depth], [lx + label_size[0] - 1.2, label_y0 + label_size[1] - 0.6, zC - label_depth + 0.15]);
    color("#30343a") for (i = [0 : 7])
        translate([lx + 5 + floor(i / 4) * 44, label_y0 + label_size[1] - 7.6 - (i % 4) * 6.1, zC - label_depth + 0.15])
            linear_extrude(0.03) text(zone_names[i], size = 3.3, font = "Liberation Sans");
}
module dressing() {
    color("#f3f0e8") for (i = [0 : len(led_c) - 1]) if (led_label[i] != "")
        translate([led_c[i][0], mark_y, zF - mark_depth]) linear_extrude(0.1)
            text(led_label[i], size = mark_size, font = mark_font, halign = "center", valign = "center");
    color("#f3f0e8") translate([wordmark_at[0], wordmark_at[1], zF - mark_depth]) linear_extrude(0.1)
        text(wordmark, size = wordmark_size, font = mark_font, halign = "right", valign = "center", spacing = 1.05);
    color("#40464d") for (i = [0, 1])
        translate([btn_c[i][0], btn_c[i][1], zF + btn_proud - mark_depth]) linear_extrude(0.1) button_icon(i);
}
module front_parts(with_cover = true) {
    color(col_front) fascia();
    dressing();
    color(col_pipe, 0.9) lightpipe();
    color(col_btn) for (i = [0, 1]) button(i);
    color(col_knob) knob();
    if (with_cover) { color(col_front) cover(); label_dressing(); }
}
module assembly(with_cover = true) {
    color(col_base) base();
    board_colored();
    color(col_bridge) bridge();
    front_parts(with_cover);
}
module exploded() {
    color(col_base) base();
    translate([0, 0, 34]) board_colored(display = false);
    translate([0, 0, 60]) color(col_bridge) bridge();
    translate([0, 0, 76]) display_colored();
    translate([0, 0, 96]) { color(col_pipe, 0.9) lightpipe(); color(col_btn) for (i = [0, 1]) button(i); }
    translate([0, 0, 118]) { color(col_front) fascia(); dressing(); }
    translate([0, -30, 132]) { color(col_front) cover(); label_dressing(); }
    translate([0, 0, 134]) color(col_knob) knob();
}
// Cuts. Each part is cut on its own so that the cut faces keep its colour.
// "section" is horizontal, through the display, the FPC fold and the encoder,
// seen from below. "section_side" is vertical, through the fuse well, seen
// from the right: it shows the air path and the roof.
cut_mode = part == "section_side" ? 1 : 0;
module cut() intersection() {
    children();
    if (cut_mode == 0) box([ox0 - 5, disp_c[1] + 0.4, z_back - 5], [ox1 + 5, fy1 + 5, knob_z1 + 5]);
    else box([ox0 - 5, oy0 - 5, z_back - 5], [fuse_c[0][0], fy1 + 5, knob_z1 + 5]);
}
module section() {
    color(col_base) cut() base();
    color(col_pcb) cut() pcb();
    color(col_part) cut() board_parts("board");
    color(col_mate) cut() board_parts("mate");
    color(col_glass) cut() panel_solid();
    color("#c98a2b") cut() fpc_solid();
    color(col_bridge) cut() bridge();
    color(col_front) cut() fascia();
    color("#4a5057") cut() cover();
    color(col_knob) cut() knob();
    color(col_btn) cut() for (i = [0, 1]) button(i);
}

module solid(name) {
    if (name == "base") base();
    if (name == "fascia") fascia();
    if (name == "cover") cover();
    if (name == "bridge") bridge();
    if (name == "knob") knob();
    if (name == "buttons") for (i = [0, 1]) button(i);
    if (name == "lightpipe") lightpipe();
    if (name == "board") { pcb(); board_parts("board"); }
    if (name == "display") { panel_solid(); fpc_solid(); }
    if (name == "plugs") board_parts("mate");
    if (name == "zones") zone_free();
    // The test-fit kit, as printed, in its assembled position.
    if (name == "tfmain") tf_main();
    if (name == "tfaddons") tf_addons();
    if (name == "tfboard") { tf_main(); tf_addons(); }
    if (name == "tfplugs") tf_plugs_all();
    if (name == "tfpanel") tf_panel(cable = false);
    if (name == "tails") tf_tails_all();
}

// Drilling template, 1:1, seen from the room side.
module template2d() {
    module cross(p, label, at) translate(p) {
        for (a = [0, 90]) rotate(a) square([14, 0.25], center = true);
        difference() { circle(d = 6); circle(d = 5.5); }
        translate(at) text(label, size = 3, font = mark_font, halign = "center");
    }
    hang_y = keyhole_y + keyhole_slot;
    difference() { outer2d(); offset(delta = -0.3) outer2d(); }
    for (x = keyhole_x) cross([x, hang_y], "wall screw", [0, 9]);
    cross(lower_fix, "third screw, fit last", [0, -13]);
    for (k = rear_ko) translate(k) difference() { circle(d = rear_ko_d); circle(d = rear_ko_d - 0.5); }
    translate([board_w / 2, rear_ko[0][1] + 14]) text("circles: cable from behind, either side", size = 2.6, font = mark_font, halign = "center");
    translate([board_w / 2, 62]) text(str(wordmark, " drilling template, print at 100 %"), size = 3.4, font = mark_font, halign = "center");
    translate([board_w / 2, 55]) text(str("upper screws ", keyhole_x[1] - keyhole_x[0], " mm apart, level"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 49]) text(str("third screw ", hang_y - lower_fix[1], " mm below, on the centre line"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 43]) text("leave screw heads 3 mm proud; hang, then slide down", size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 37]) text(str("outline of the base ", ox1 - ox0, " x ", oy1 - oy0, " mm; check: this line is 100 mm"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2 - 50, 32]) square([100, 0.3]);
    for (x = [-50, 50]) translate([board_w / 2 + x - 0.15, 30.5]) square([0.3, 3.3]);
}

// ---- What to show ---------------------------------------------------------------------
module show() { if (wallview) rotate([90, 0, 0]) children(); else children(); }
if (part == "assembly") show() assembly();
if (part == "open") show() assembly(with_cover = false);
if (part == "exploded") show() exploded();
if (part == "section" || part == "section_side") show() section();
if (part == "board") show() board_colored();
if (part == "zones") { %pcb(); zone_free(); }
if (part == "check") intersection() { solid(check_a); solid(check_b); }
// One solid in its assembled position, by the names solid() knows.
if (part == "solid") solid(check_a);
if (part == "template") template2d();
for (n = ["base", "fascia", "cover", "bridge", "knob", "lightpipe"]) if (part == n) solid(n);
if (part == "button_back") button(0);
if (part == "button_home") button(1);

// Print orientation: the face that should look best is on the bed, and no
// part needs support.
if (part == "print_base") translate([-ox0, -oy0, -z_back]) base();
if (part == "print_fascia") translate([-ox0, fy1, zF]) rotate([180, 0, 0]) fascia();
if (part == "print_cover") translate([-ox0, brow_y, zC]) rotate([180, 0, 0]) cover();
if (part == "print_bridge") translate([0, disp_y1 + 10, z_deck_top]) rotate([180, 0, 0]) bridge();
if (part == "print_knob") translate([-enc_c[0], enc_c[1], knob_z1]) rotate([180, 0, 0]) knob();
if (part == "print_button_back") translate([-btn_c[0][0], -btn_c[0][1], -btn_flange_z0]) button(0);
if (part == "print_button_home") translate([-btn_c[1][0], -btn_c[1][1], -btn_flange_z0]) button(1);
// The test-fit kit.
if (part == "tf_board") show() tf_board_colored();
if (part == "tf_scene") show() tf_scene();
if (part == "tf_kit") tf_kit();
if (part == "tf_underside") tf_underside();
for (n = ["tfmain", "tfaddons", "tfboard", "tfplugs", "tfpanel", "tails"]) if (part == n) solid(n);
if (part == "print_tf_board") tf_print_main();
if (part == "print_tf_c1") tf_print_addon("C1");
if (part == "print_tf_k1") tf_print_addon("K1");
for (i = [0 : len(tf_headers) - 1]) if (part == str("print_tf_plug_", tf_headers[i][6], "p")) tf_print_plug(i);
if (part == "print_tf_panel") tf_print_panel();
if (part == "print_lightpipe") translate([-led_c[0][0], zF, -(led_y - lp_size / 2)]) rotate([90, 0, 0]) lightpipe();
