// ZoneFlow enclosure, revision A concept.
//
// Wall-mounted, two compartments: electronics above, wiring below.
// Seven printed parts: base, fascia, wiring cover, display carrier, knob,
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
oy1 = board_h + top_gap + wall;          // outside, top
part_y0 = header_face_y;                 // partition, wiring side
part_y1 = part_y0 + part_wall;           // partition, board side
oy0 = part_y0 - bay_h - wall;            // outside, bottom
ix0 = ox0 + wall;
ix1 = ox1 - wall;
iy0 = oy0 + wall;
iy1 = oy1 - wall;
z_pcb_bot = -board_t;
z_floor = z_pcb_bot - bottom_clearance;  // inside of the back wall
z_back = z_floor - back_t;               // the surface against the wall
zF = z_fascia_outer;
zFi = z_fascia_inner;
zC = zF + cover_proud;
zCi = z_cover_inner;
cover_y1 = part_y0 - reveal;             // top wall of the wiring cover
brow_y = part_y0 + cover_overlap;        // its top edge laps over the fascia
z_brow = zF + 0.15;                      // underside of that lap
z_deck_top = z_panel_back - panel_pad;
z_deck_bot = z_deck_top - deck_t;
disp_x0 = disp_origin[0];
disp_y0 = disp_origin[1];
disp_x1 = disp_x0 + disp_outline[0];
disp_y1 = disp_y0 + disp_outline[1];
led_y = led_c[0][1];
knob_z0 = zF + knob_gap;
knob_z1 = enc_shaft_top_z + knob_roof;
eps = 0.01;

assert(abs(part_y1 + part_gap) < eps, "partition does not sit part_gap from the board edge");
assert(abs(zC - skin - zCi) < eps, "cover_proud and z_cover_inner disagree");
assert(part_gap + part_wall <= -header_face_y + eps, "partition is thicker than the header overhang");

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
module fascia_clip2d() rect(ox0 - 1, part_y0, ox1 + 1, oy1 + 1);
module cover_clip2d() rect(ox0 - 1, oy0 - 1, ox1 + 1, cover_y1);
// Convex profile for the cover's chamfer; taller than the cover, so that the
// top edge is cut square afterwards.
module cover2d() intersection() { outer2d(); rect(ox0 - 1, oy0 - 1, ox1 + 1, brow_y + 20); }

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

module louvres() {
    ty = louvre_gap * sqrt(2);
    run = wall + 0.2;
    for (y = [louvre_y[0] : louvre_pitch : louvre_y[1]])
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
        // Fascia screws through the top wall, countersunk.
        for (x = top_screw_x) {
            along_y([x, oy1 + 1, top_screw_z], m3_clear, wall + 2);
            translate([x, oy1 + eps, top_screw_z]) rotate([90, 0, 0])
                cylinder(d1 = m3_csk_d, d2 = m3_clear, h = (m3_csk_d - m3_clear) / 2);
        }
        // Ventilation.
        louvres();
        mirror_x() louvres();
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
    difference() {
        union() {
            difference() {
                intersection() {
                    union() {
                        chamfer_slab(z_split + reveal_h, zF, chamfer) outer2d();
                        ext(z_split, z_split + reveal_h + eps) offset(delta = -reveal) outer2d();
                    }
                    ext(z_split - 1, zF + 1) fascia_clip2d();
                }
                ext(z_split - 1, zFi) intersection() { inner2d(); fascia_clip2d(); }
            }
            intersection() { lip(); box([ox0, part_y1, lip_z0 - 1], [ox1, oy1, zF]); }
            // Its half of the partition, with teeth between the connectors.
            box([ix0 - eps, part_y0, 0.3], [ix1 + eps, part_y1, zFi + eps]);
            // Pillars that take the two screws through the top wall.
            for (x = top_screw_x) box([x - 4, iy1 - fit - 7.25, lip_z0], [x + 4, iy1 - fit, zFi + eps]);
            // Ribs that locate the display panel sideways.
            box([disp_x0 - f - rib_t, cy - 10, rib_z0], [disp_x0 - f, cy + 10, zFi + eps]);
            box([cx - 12, disp_y1 + f, rib_z0], [cx + 12, disp_y1 + f + rib_t, zFi + eps]);
            box([cx - 12, disp_y0 - f - rib_t, rib_z0], [cx + 12, disp_y0 - f, zFi + eps]);
            for (y = [disp_y0 + 0.5, disp_y1 - 5.5])
                box([disp_x1 + f, y, rib_z0], [disp_x1 + f + rib_t, y + 5, zFi + eps]);
            // Channel that grips the light-pipe rail.
            for (s = [-1, 1]) {
                y = led_y + s * (lp_size / 2 + lp_fit + lp_chan_wall / 2);
                box([led_c[0][0] - 2.5, y - lp_chan_wall / 2, lp_rail_z[0] - 0.2],
                    [led_c[len(led_c) - 1][0] + 2.5, y + lp_chan_wall / 2, zFi + eps]);
            }
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
        // Label recess and markings.
        ext(zF - label_depth, zF + 1) rect(board_w / 2 - label_size[0] / 2, label_y0,
                                         board_w / 2 + label_size[0] / 2, label_y0 + label_size[1]);
        for (i = [0 : len(led_c) - 1]) if (led_label[i] != "")
            translate([led_c[i][0], mark_y, zF - mark_depth]) linear_extrude(1)
                text(led_label[i], size = mark_size, font = mark_font, halign = "center", valign = "center");
        translate(led_c[0]) ext(zF - 0.3, zF + 1) difference() { circle(d = 6.4); circle(d = 5.2); }
        // Openings in the partition for the edge connectors.
        for (t = terminals)
            box([t[1] - comp_clear, part_y0 - 1, -1], [t[2] + comp_clear, part_y1 + 1, header_h + comp_clear]);
        box([usb_x - 6.5, part_y0 - 1, -1], [terminals[0][1] - comp_clear + eps, part_y1 + 1, usb_z + 3.7]);
        box([qwiic_x - 3.75, part_y0 - 1, -1], [qwiic_x + 3.75, part_y1 + 1, 4.4]);
        // Slots for the wiring cover's toes.
        for (x = toe_x)
            box([x - toe_w / 2 - fit, part_y0 - 1, 6.9], [x + toe_w / 2 + fit, part_y1 + 1, 8.75]);
        // Screw holes in the pillars, along y.
        for (x = top_screw_x) translate([x, iy1 - fit, top_screw_z]) rotate([-90, 0, 0]) fastener_hole();
    }
}

// ---- Wiring cover -----------------------------------------------------------
module cover() {
    wall_y0 = cover_y1 - 2.0;
    difference() {
        union() {
            difference() {
                union() {
                    intersection() {
                        chamfer_slab(z_split + reveal_h, zC, chamfer) cover2d();
                        box([ox0 - 1, oy0 - 1, z_split], [ox1 + 1, brow_y, zC + 1]);
                    }
                    ext(z_split, z_split + reveal_h + eps) intersection() { offset(delta = -reveal) outer2d(); cover_clip2d(); }
                }
                // Above the top wall only the brow remains; it laps over the
                // fascia's lower edge, hides the seam and holds that edge down.
                box([ox0 - 1, cover_y1, z_split - 1], [ox1 + 1, brow_y + 1, z_brow]);
                ext(z_split - 1, zCi) intersection() { inner2d(); rect(ox0, oy0, ox1, wall_y0); }
                // The top wall stops above the plugs.
                box([ix0, wall_y0 - eps, z_split - 1], [ix1, cover_y1 + 1, plug_top_z + 0.3]);
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
        translate([board_w / 2, (cover_y1 + oy0) / 2 + 4, zC - mark_depth]) linear_extrude(1)
            text(wordmark, size = 7, font = mark_font, halign = "center", valign = "center", spacing = 1.05);
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
        box([disp_x1 - 7.76, disp_fpc_root_y[0] - 0.7, z_deck_bot - 1], [dx1 + 1, disp_fpc_root_y[1] + 0.85, z_deck_top + 1]);
        for (h = holes) if (h[4] == "screw" && h[5] > 8) translate([h[1], h[2], 0]) {
            translate([0, 0, -1]) cylinder(d = m3_clear, h = z_deck_top + 2);
            // Countersunk, so that the part prints deck-down without support.
            translate([0, 0, z_deck_top - (m3_csk_d - m3_clear) / 2])
                cylinder(d1 = m3_clear, d2 = m3_csk_d, h = (m3_csk_d - m3_clear) / 2 + eps);
        }
    }
}

// ---- Knob -------------------------------------------------------------------
module knob() {
    hub_z0 = enc_bushing_top_z + enc_travel + 1.6;
    r = enc_shaft_d / 2 + knob_bore_fit;
    translate([enc_c[0], enc_c[1], 0]) {
        difference() {
            hull() {
                translate([0, 0, knob_z0]) cylinder(d = knob_d, h = knob_z1 - knob_z0 - 1.5);
                translate([0, 0, knob_z1 - eps]) cylinder(d = knob_d - 3, h = eps);
            }
            translate([0, 0, knob_z0 - eps]) cylinder(d = knob_d - 2 * knob_wall, h = knob_z1 - knob_roof - knob_z0);
            for (i = [0 : knob_flutes - 1]) rotate(i * 360 / knob_flutes)
                translate([knob_d / 2 + 0.3, 0, knob_z0 - 1]) cylinder(d = 1.5, h = knob_z1 - knob_z0 - 1, $fn = 12);
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
module board_model() { pcb(); board_parts("board"); board_parts("display"); board_parts("mate"); }
module display_colored() {
    color(col_glass) for (p = part_prims) if (p[0] == "DS1") prim(p);
    color("#c98a2b", 0.8) for (p = part_prims) if (p[0] == "DS1-FPC") prim(p);
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

// ---- Assembly and views -----------------------------------------------------------
// Render only: a paper label in the recess and the markings filled with
// white paint, which is how the printing guide says to finish them.
zone_names = ["1  Front lawn", "2  Back lawn", "3  Rose bed", "4  Vegetables",
              "5  Hedge", "6  Drip line", "7  Pots", "8  Spare"];
module dressing() {
    lx = board_w / 2 - label_size[0] / 2 + 0.6;
    color("#f3f0e8") box([lx, label_y0 + 0.6, zF - label_depth], [lx + label_size[0] - 1.2, label_y0 + label_size[1] - 0.6, zF - label_depth + 0.15]);
    color("#30343a") for (i = [0 : 7])
        translate([lx + 5 + floor(i / 4) * 44, label_y0 + label_size[1] - 7.6 - (i % 4) * 6.1, zF - label_depth + 0.15])
            linear_extrude(0.03) text(zone_names[i], size = 3.3, font = "Liberation Sans");
    color("#f3f0e8") for (i = [0 : len(led_c) - 1]) if (led_label[i] != "")
        translate([led_c[i][0], mark_y, zF - mark_depth]) linear_extrude(0.1)
            text(led_label[i], size = mark_size, font = mark_font, halign = "center", valign = "center");
    color("#40464d") for (i = [0, 1])
        translate([btn_c[i][0], btn_c[i][1], zF + btn_proud - mark_depth]) linear_extrude(0.1) button_icon(i);
}
module front_parts(with_cover = true, dz = 0) {
    translate([0, 0, dz]) {
        color(col_front) fascia();
        dressing();
        color(col_pipe, 0.9) lightpipe();
        color(col_btn) for (i = [0, 1]) button(i);
    }
    translate([0, 0, dz * 1.5]) color(col_knob) knob();
    if (with_cover) translate([0, 0, dz]) color(col_front) cover();
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
    translate([0, -16, 118]) color(col_front) cover();
    translate([0, 0, 134]) color(col_knob) knob();
}
// Cut through the display, FPC fold and encoder, seen from below. Each part
// is cut on its own so that the cut faces keep the part's colour.
module cut() intersection() {
    children();
    box([ox0 - 5, disp_c[1] + 0.4, z_back - 5], [ox1 + 5, oy1 + 5, knob_z1 + 5]);
}
module section() {
    color(col_base) cut() base();
    color(col_pcb) cut() pcb();
    color(col_part) cut() board_parts("board");
    color(col_glass) cut() for (p = part_prims) if (p[0] == "DS1") prim(p);
    color("#c98a2b") cut() for (p = part_prims) if (p[0] == "DS1-FPC") prim(p);
    color(col_bridge) cut() bridge();
    color(col_front) cut() fascia();
    color(col_knob) cut() knob();
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
    if (name == "display") board_parts("display");
    if (name == "plugs") board_parts("mate");
    if (name == "zones") zone_free();
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
    translate([board_w / 2, 52]) text(str(wordmark, " drilling template, print at 100 %"), size = 3.4, font = mark_font, halign = "center");
    translate([board_w / 2, 45]) text(str("upper screws ", keyhole_x[1] - keyhole_x[0], " mm apart, level"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 39]) text(str("third screw ", hang_y - lower_fix[1], " mm below, on the centre line"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 33]) text("leave screw heads 3 mm proud; hang, then slide down", size = 3, font = mark_font, halign = "center");
    translate([board_w / 2, 27]) text(str("outline ", ox1 - ox0, " x ", oy1 - oy0, " mm; check: this line is 100 mm"), size = 3, font = mark_font, halign = "center");
    translate([board_w / 2 - 50, 22]) square([100, 0.3]);
    for (x = [-50, 50]) translate([board_w / 2 + x - 0.15, 20.5]) square([0.3, 3.3]);
}

// ---- What to show ---------------------------------------------------------------------
module show() { if (wallview) rotate([90, 0, 0]) children(); else children(); }
if (part == "assembly") show() assembly();
if (part == "open") show() assembly(with_cover = false);
if (part == "exploded") show() exploded();
if (part == "section") show() section();
if (part == "board") show() board_colored();
if (part == "zones") { %pcb(); zone_free(); }
if (part == "check") intersection() { solid(check_a); solid(check_b); }
if (part == "template") template2d();
for (n = ["base", "fascia", "cover", "bridge", "knob", "lightpipe"]) if (part == n) solid(n);
if (part == "button_back") button(0);
if (part == "button_home") button(1);

// Print orientation: the face that should look best is on the bed, and no
// part needs support.
if (part == "print_base") translate([-ox0, -oy0, -z_back]) base();
if (part == "print_fascia") translate([-ox0, oy1, zF]) rotate([180, 0, 0]) fascia();
if (part == "print_cover") translate([-ox0, cover_y1, zC]) rotate([180, 0, 0]) cover();
if (part == "print_bridge") translate([0, disp_y1 + 10, z_deck_top]) rotate([180, 0, 0]) bridge();
if (part == "print_knob") translate([-enc_c[0], enc_c[1], knob_z1]) rotate([180, 0, 0]) knob();
if (part == "print_button_back") translate([-btn_c[0][0], -btn_c[0][1], -btn_flange_z0]) button(0);
if (part == "print_button_home") translate([-btn_c[1][0], -btn_c[1][1], -btn_flange_z0]) button(1);
if (part == "print_lightpipe") translate([-led_c[0][0], zF, -(led_y - lp_size / 2)]) rotate([90, 0, 0]) lightpipe();
